#include "ArchiveExtractor.h"

#include "PathUtils.h"
#include "ZipReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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

    QDirIterator walker(result.rootPath, QDir::Files | QDir::NoDotAndDotDot,
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

        const QByteArray payload = archive->read(name, limits.maxFileBytes);
        if (payload.isEmpty() && entry->uncompressedSize > 0) {
            ++result.skipped;
            continue;
        }

        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            result.error = QStringLiteral("Cannot write %1").arg(target);
            return result;
        }
        if (out.write(payload) != payload.size()) {
            result.error = QStringLiteral("Ran out of space writing %1").arg(relative);
            return result;
        }
        out.close();

        ExtractedFile file;
        file.localPath = target;
        file.relativePath = relative;
        file.size = payload.size();
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
