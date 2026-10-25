#include "ArchiveExtractor.h"

#include "PathUtils.h"
#include "ZipReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QElapsedTimer>
#include <QStandardPaths>
#include <QSet>

namespace vsp {
namespace {

/// Rebuilds an archive-relative path out of individually sanitised components.
///
/// A ".." component is refused outright: there is no safe reading of it, and
/// guessing what the archive meant would be worse than skipping the file. A
/// leading "/" is simply dropped, which is what every extractor does -- the
/// entry still lands inside the destination, so it is neutralised rather than
/// lost. Returns an empty string when nothing usable survives.
QString safeRelativePath(const QString &raw, int maxDepth)
{
    QString normalised = raw;
    normalised.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList safeParts;
    const QStringList parts = normalised.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String(".") || part == QLatin1String(".."))
            return {};
        // A Windows drive letter or a UNC fragment has no business here either.
        if (part.contains(QLatin1Char(':')))
            return {};

        const QString safe = path::sanitizeFileName(part);
        if (safe.isEmpty() || safe == QLatin1String("unnamed"))
            return {};
        safeParts.append(safe);
    }

    if (safeParts.isEmpty() || safeParts.size() > maxDepth)
        return {};
    return safeParts.join(QLatin1Char('/'));
}

} // namespace

namespace {
QString archiveTool()
{
#ifdef Q_OS_WIN
    // Use the Windows component, never an executable supplied by an archive
    // or discovered in the working directory.
    return QDir(qEnvironmentVariable("SystemRoot", "C:/Windows"))
        .filePath(QStringLiteral("System32/tar.exe"));
#else
    return QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
#endif
}

bool isZip(const QString &name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    return suffix == QLatin1String("zip") || suffix == QLatin1String("vpk");
}

}

QStringList ArchiveExtractor::entryNames(const QString &archivePath, QString *error)
{
    if (error) error->clear();
    if (isZip(archivePath)) {
        ZipReader zip;
        if (zip.open(archivePath)) return zip.entryNames();
        if (error) *error = zip.errorString();
        return {};
    }
    const QString tool = archiveTool();
    if (tool.isEmpty() || !QFileInfo(tool).isFile()) {
        if (error) *error = QStringLiteral("An archive reader is unavailable on this system");
        return {};
    }
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(tool, {QStringLiteral("-tf"), QFileInfo(archivePath).absoluteFilePath()});
    process.closeWriteChannel();
    QByteArray listing;
    QElapsedTimer clock;
    clock.start();
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50);
        listing += process.readAllStandardOutput();
        if (listing.size() > 8 * 1024 * 1024 || clock.elapsed() > 30000) {
            process.kill(); process.waitForFinished();
            if (error) *error = QStringLiteral("Archive listing exceeds its size or time limit");
            return {};
        }
    }
    listing += process.readAllStandardOutput();
    if (listing.size() > 8 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Archive listing exceeds its size or time limit");
        return {};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (error) *error = QStringLiteral("Cannot read this archive; it may be damaged, encrypted or unsupported");
        return {};
    }
    QStringList names = QString::fromUtf8(listing).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    if (names.size() > 20000) {
        if (error) *error = QStringLiteral("Archive contains too many entries");
        return {};
    }
    // Reject link/device records before extracting individual members. The
    // archive tool writes only to stdout, and Qt creates the regular files.
    QProcess types;
    types.setStandardErrorFile(QProcess::nullDevice());
    types.start(tool, {QStringLiteral("-tvf"), QFileInfo(archivePath).absoluteFilePath()});
    types.closeWriteChannel();
    QByteArray details;
    clock.restart();
    while (types.state() != QProcess::NotRunning) {
        types.waitForReadyRead(50);
        details += types.readAllStandardOutput();
        if (details.size() > 16 * 1024 * 1024 || clock.elapsed() > 30000) {
            types.kill(); types.waitForFinished();
            if (error) *error = details.size() > 16 * 1024 * 1024
                ? QStringLiteral("Cannot inspect archive file types")
                : QStringLiteral("Archive inspection timed out");
            return {};
        }
    }
    details += types.readAllStandardOutput();
    if (types.exitCode() != 0 || types.exitStatus() != QProcess::NormalExit
        || details.size() > 16 * 1024 * 1024) {
        if (error) *error = QStringLiteral("Cannot inspect archive file types");
        return {};
    }
    for (const QByteArray &line : details.split('\n')) {
        if (!line.isEmpty() && line.at(0) != '-' && line.at(0) != 'd') {
            if (error) *error = QStringLiteral("Archive links and special files cannot be sent");
            return {};
        }
    }
    for (QString &name : names) {
        if (name.endsWith(QLatin1Char('\r'))) name.chop(1);
    }
    return names;
}

ExtractionResult ArchiveExtractor::extractArchive(const QString &archivePath,
                                                  const QString &prefix,
                                                  const QString &destinationDir,
                                                  const ExtractionLimits &limits)
{
    if (isZip(archivePath)) {
        ZipReader zip;
        ExtractionResult result;
        if (!zip.open(archivePath)) { result.error = zip.errorString(); return result; }
        return extractSubtree(&zip, prefix, destinationDir, limits);
    }
    ExtractionResult result;
    QString error;
    const QStringList names = entryNames(archivePath, &error);
    if (!error.isEmpty()) { result.error = error; return result; }
    QDir destination(destinationDir);
    if (!destination.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("Cannot create archive workspace"); return result;
    }
    // Repack once to a stored ZIP stream. The reader never writes archive
    // paths, and the existing bounded ZIP extractor validates every output.
    QTemporaryFile temporary(destination.filePath(QStringLiteral("reader-XXXXXX.zip")));
    if (!temporary.open()) { result.error = temporary.errorString(); return result; }
    QProcess process;
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(archiveTool(), {QStringLiteral("-cf"), QStringLiteral("-"),
                                 QStringLiteral("--format=zip"), QStringLiteral("--options=zip:compression=store"),
                                 QStringLiteral("@") + QFileInfo(archivePath).absoluteFilePath()});
    process.closeWriteChannel();
    QElapsedTimer clock;
    clock.start();
    qint64 bytes = 0;
    const qint64 outputLimit = limits.maxTotalBytes + qint64(names.size()) * 4096 + 1024 * 1024;
    auto drain = [&] {
        const QByteArray chunk = process.readAllStandardOutput();
        bytes += chunk.size();
        return bytes <= outputLimit && temporary.write(chunk) == chunk.size();
    };
    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(50);
        if (!drain() || clock.elapsed() > 300000) {
            process.kill(); process.waitForFinished();
            result.error = QStringLiteral("Archive extraction exceeds its size or time limit");
            return result;
        }
    }
    if (!drain() || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        result.error = QStringLiteral("Cannot extract archive contents"); return result;
    }
    temporary.close();
    ZipReader zip;
    if (!zip.open(temporary.fileName())) { result.error = zip.errorString(); return result; }
    return extractSubtree(&zip, prefix, destinationDir, limits);
}
ExtractionResult ArchiveExtractor::collectDirectory(const QString &sourceDir,
                                                    const ExtractionLimits &limits)
{
    ExtractionResult result;

    const QDir root(sourceDir);
    if (!root.exists()) {
        result.error = QStringLiteral("Folder not found");
        return result;
    }
    result.rootPath = QDir::cleanPath(root.absolutePath());

    QDirIterator walker(result.rootPath, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
    while (walker.hasNext()) {
        const QString filePath = walker.next();
        const QFileInfo fileInfo = walker.fileInfo();

        if (result.files.size() >= limits.maxFiles) {
            result.error = QStringLiteral("That folder holds more than %1 files")
                               .arg(limits.maxFiles);
            return result;
        }
        if (fileInfo.size() > limits.maxFileBytes) {
            ++result.skipped;
            continue;
        }
        if (result.totalBytes + fileInfo.size() > limits.maxTotalBytes) {
            result.error = QStringLiteral("That folder holds more than %1")
                               .arg(path::humanSize(limits.maxTotalBytes));
            return result;
        }

        const QString relative = root.relativeFilePath(filePath);
        const QString safe = safeRelativePath(relative, limits.maxDepth);
        if (safe.isEmpty()) {
            ++result.skipped;
            continue;
        }

        ExtractedFile file;
        file.localPath = filePath;
        file.relativePath = safe;
        file.size = fileInfo.size();
        result.files.append(file);
        result.totalBytes += file.size;
    }

    if (result.files.isEmpty()) {
        result.error = QStringLiteral("That folder has nothing in it to send");
        return result;
    }

    result.ok = true;
    return result;
}

ExtractionResult ArchiveExtractor::extractSubtree(ZipReader *archive,
                                                  const QString &archivePrefix,
                                                  const QString &destinationDir,
                                                  const ExtractionLimits &limits)
{
    ExtractionResult result;

    if (!archive || !archive->isOpen()) {
        result.error = QStringLiteral("Archive is not open");
        return result;
    }

    QDir destination(destinationDir);
    if (!destination.exists() && !destination.mkpath(QStringLiteral("."))) {
        result.error = QStringLiteral("Cannot create %1").arg(destinationDir);
        return result;
    }

    const QString root = QDir::cleanPath(destination.absolutePath());
    const QString rootWithSlash = root.endsWith(QLatin1Char('/')) ? root
                                                                  : root + QLatin1Char('/');
    result.rootPath = root;
    QSet<QString> written;

    const QStringList names = archive->entryNames();
    for (const QString &name : names) {
        if (!name.startsWith(archivePrefix, Qt::CaseInsensitive))
            continue;

        const QString inside = name.mid(archivePrefix.size());
        if (inside.isEmpty() || inside.endsWith(QLatin1Char('/')))
            continue;                       // directory entry, made implicitly

        const ZipReader::Entry *entry = archive->entry(name);
        if (!entry || entry->isDirectory)
            continue;

        if (result.files.size() >= limits.maxFiles) {
            result.error = QStringLiteral("Archive holds more than %1 files")
                               .arg(limits.maxFiles);
            return result;
        }
        if (static_cast<qint64>(entry->uncompressedSize) > limits.maxFileBytes) {
            ++result.skipped;
            continue;
        }
        if (result.totalBytes + static_cast<qint64>(entry->uncompressedSize)
            > limits.maxTotalBytes) {
            result.error = QStringLiteral("Archive unpacks to more than %1")
                               .arg(path::humanSize(limits.maxTotalBytes));
            return result;
        }

        const QString relative = safeRelativePath(inside, limits.maxDepth);
        if (relative.isEmpty()) {
            ++result.skipped;
            continue;
        }
        if (written.contains(relative.toCaseFolded())) {
            result.error = QStringLiteral("Archive contains colliding file names"); return result;
        }
        written.insert(relative.toCaseFolded());

        const QString target = QDir::cleanPath(destination.absoluteFilePath(relative));
        // Belt and braces: the components were sanitised, but the resolved path
        // is checked against the root before anything is opened for writing.
        if (!target.startsWith(rootWithSlash)) {
            ++result.skipped;
            continue;
        }

        const QString parent = QFileInfo(target).absolutePath();
        if (!QDir().mkpath(parent)) {
            result.error = QStringLiteral("Cannot create %1").arg(parent);
            return result;
        }

        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            result.error = QStringLiteral("Cannot write %1").arg(target);
            return result;
        }
        if (!archive->copyTo(name, &out, limits.maxFileBytes) || !out.commit()) {
            result.error = QStringLiteral("Cannot extract %1: %2").arg(relative, archive->errorString());
            return result;
        }

        ExtractedFile file;
        file.localPath = target;
        file.relativePath = relative;
        file.size = static_cast<qint64>(entry->uncompressedSize);
        result.files.append(file);
        result.totalBytes += file.size;
    }

    if (result.files.isEmpty()) {
        result.error = result.error.isEmpty()
                           ? QStringLiteral("Nothing usable was found in the archive")
                           : result.error;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace vsp
