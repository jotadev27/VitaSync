#include "PackageInspector.h"

#include "SfoReader.h"
#include "ThemeReader.h"
#include "ZipReader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace vsp {
namespace {

const QStringList kVideoExtensions = {
    QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("avi"),
    QStringLiteral("mov"), QStringLiteral("m4v"), QStringLiteral("mpg"),
    QStringLiteral("mpeg"), QStringLiteral("wmv"), QStringLiteral("webm")
};
const QStringList kPhotoExtensions = {
    QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
    QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("tif"),
    QStringLiteral("tiff"), QStringLiteral("webp")
};
const QStringList kMusicExtensions = {
    QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
    QStringLiteral("m4a"), QStringLiteral("ogg"), QStringLiteral("aac")
};

PackageKind kindFromCategory(const QString &category, bool looksRetail)
{
    if (category == QLatin1String("gp"))
        return PackageKind::Patch;
    if (category == QLatin1String("ac"))
        return PackageKind::AddOn;
    if (category.startsWith(QLatin1String("theme"), Qt::CaseInsensitive))
        return PackageKind::SystemTheme;
    if (category == QLatin1String("gd") || category.isEmpty())
        return looksRetail ? PackageKind::Game : PackageKind::Homebrew;
    return PackageKind::Homebrew;
}

// Retail Vita Title IDs are four letters plus five digits (PCSE00001,
// PCSB00245, ...). Homebrew tends to use a made-up ID, which is fine -- we just
// label it differently and never pretend we matched cover art for it.
bool looksLikeRetailTitleId(const QString &titleId)
{
    if (titleId.size() != 9)
        return false;
    for (int i = 0; i < 4; ++i) {
        if (!titleId.at(i).isLetter())
            return false;
    }
    for (int i = 4; i < 9; ++i) {
        if (!titleId.at(i).isDigit())
            return false;
    }
    return titleId.startsWith(QLatin1String("PCS"))
           || titleId.startsWith(QLatin1String("PCG"))
           || titleId.startsWith(QLatin1String("VCS"))
           || titleId.startsWith(QLatin1String("NPS"))
           || titleId.startsWith(QLatin1String("NPX"))
           || titleId.startsWith(QLatin1String("PCA"));
}

// Every immediate subfolder of \a rootPath, inspected exactly as a
// standalone game-folder drop would be and tagged with which fixed
// destination root it belongs to. Shared by the two NoNpDrm-shaped entry
// points below -- a parent dump's "app"/"addcont"/"license" subfolder and
// one of those three dragged on its own are, at this point, identical.
QVector<PackageInfo> inspectContentRoot(const QString &rootPath, const QString &rootName)
{
    QVector<PackageInfo> parts;
    const QFileInfoList children = QDir(rootPath).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &child : children) {
        PackageInfo info = PackageInspector::inspectFolder(child.absoluteFilePath());
        info.noNpDrmRoot = rootName;
        parts.append(info);
    }
    return parts;
}

} // namespace

QString packageKindLabel(PackageKind kind)
{
    switch (kind) {
    case PackageKind::Game:     return QStringLiteral("Game");
    case PackageKind::Patch:    return QStringLiteral("Update");
    case PackageKind::AddOn:    return QStringLiteral("Add-on");
    case PackageKind::Homebrew: return QStringLiteral("Homebrew");
    case PackageKind::FolderGame: return QStringLiteral("Game folder");
    case PackageKind::SystemTheme: return QStringLiteral("Vita Theme");
    case PackageKind::ShellTheme:  return QStringLiteral("Shell Theme");
    case PackageKind::Video:    return QStringLiteral("Video");
    case PackageKind::Photo:    return QStringLiteral("Photo");
    case PackageKind::Music:    return QStringLiteral("Music");
    case PackageKind::Unknown:  break;
    }
    return QStringLiteral("File");
}

QString PackageInfo::displayName() const
{
    if (!title.isEmpty())
        return title;
    return fileName;
}

bool PackageInfo::isDirectoryPayload() const
{
    return kind == PackageKind::SystemTheme || kind == PackageKind::ShellTheme
           || kind == PackageKind::FolderGame;
}

bool PackageInfo::isInstallable() const
{
    switch (kind) {
    case PackageKind::Game:
    case PackageKind::Patch:
    case PackageKind::AddOn:
    case PackageKind::Homebrew:
    case PackageKind::FolderGame:
    case PackageKind::SystemTheme:
    case PackageKind::ShellTheme:
        return valid;
    default:
        return false;
    }
}

PackageKind PackageInspector::kindForExtension(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (kVideoExtensions.contains(suffix))
        return PackageKind::Video;
    if (kPhotoExtensions.contains(suffix))
        return PackageKind::Photo;
    if (kMusicExtensions.contains(suffix))
        return PackageKind::Music;
    if (suffix == QLatin1String("vpk") || suffix == QLatin1String("zip"))
        return PackageKind::Unknown;   // needs the archive opened to be sure
    return PackageKind::Unknown;
}

bool PackageInspector::looksLikeTitleId(const QString &id)
{
    return looksLikeRetailTitleId(id);
}

PackageInfo PackageInspector::inspect(const QString &localPath)
{
    PackageInfo info;
    info.localPath = localPath;

    const QFileInfo fileInfo(localPath);
    info.fileName = fileInfo.fileName();
    info.fileSize = fileInfo.size();

    if (fileInfo.isDir())
        return inspectFolder(localPath);

    if (!fileInfo.exists() || !fileInfo.isFile()) {
        info.error = QStringLiteral("File not found");
        return info;
    }
    if (info.fileSize == 0) {
        info.error = QStringLiteral("File is empty");
        return info;
    }

    const QString suffix = fileInfo.suffix().toLower();
    const PackageKind byExtension = kindForExtension(info.fileName);
    if (byExtension != PackageKind::Unknown) {
        info.kind = byExtension;
        info.valid = true;
        return info;
    }

    if (suffix != QLatin1String("vpk") && suffix != QLatin1String("zip")) {
        info.kind = PackageKind::Unknown;
        info.valid = true;      // still transferable, just not identifiable
        return info;
    }

    ZipReader archive;
    if (!archive.open(localPath)) {
        info.error = archive.errorString();
        return info;
    }

    // Themes are checked first: they carry no param.sfo, and the two formats
    // are unambiguous about which they are (theme.xml vs colors.txt).
    const ThemeInfo theme = ThemeReader::inspectArchive(&archive, fileInfo.completeBaseName());
    if (theme.flavour != ThemeFlavour::None) {
        info.kind = theme.flavour == ThemeFlavour::SystemTheme ? PackageKind::SystemTheme
                                                               : PackageKind::ShellTheme;
        info.themeFlavour = theme.flavour;
        info.themeFolderName = theme.folderName;
        info.archivePrefix = theme.archivePrefix;
        info.title = theme.displayName();
        info.provider = theme.provider;
        info.appVersion = theme.contentVersion;
        info.entryCount = theme.fileCount;
        info.unpackedSize = theme.unpackedSize;
        info.iconPng = theme.previewPng;
        info.accentColors = theme.accentColors;
        info.valid = theme.valid;
        info.error = theme.error;
        return info;
    }

    const QByteArray sfoBlob = archive.read(QStringLiteral("sce_sys/param.sfo"));
    if (!sfoBlob.isEmpty()) {
        SfoReader sfo;
        if (sfo.parse(sfoBlob)) {
            info.titleId    = sfo.titleId();
            info.title      = sfo.title();
            info.appVersion = sfo.appVersion();
            info.category   = sfo.category();
            info.contentId  = sfo.string(QStringLiteral("CONTENT_ID"));
            info.kind = kindFromCategory(info.category, looksLikeRetailTitleId(info.titleId));
            info.valid = true;
        } else {
            info.error = sfo.errorString();
        }
    }

    if (!info.valid) {
        // No param.sfo: still a legitimate package shape for some themes and
        // for plain content archives. Fall back to structural hints.
        const bool hasEboot = archive.findEntryInsensitive(QStringLiteral("eboot.bin")) != nullptr;

        if (hasEboot) {
            info.kind = PackageKind::Homebrew;
            info.title = fileInfo.completeBaseName();
            info.valid = true;
            info.error.clear();
        } else if (info.error.isEmpty()) {
            info.error = QStringLiteral("No param.sfo in package");
        }
    }

    // Icons are small; cap the read so a mislabelled entry cannot blow up.
    for (const QString &candidate : { QStringLiteral("sce_sys/icon0.png"),
                                      QStringLiteral("sce_sys/pic0.png") }) {
        info.iconPng = archive.read(candidate, 4 * 1024 * 1024);
        if (!info.iconPng.isEmpty())
            break;
    }

    return info;
}

PackageInfo PackageInspector::inspectFolder(const QString &localPath)
{
    PackageInfo info;
    info.localPath = localPath;
    info.localIsDirectory = true;
    info.kind = PackageKind::FolderGame;

    const QDir folder(localPath);
    info.fileName = folder.dirName();

    if (!folder.exists()) {
        info.error = QStringLiteral("Folder not found");
        return info;
    }

    // param.sfo is the authority on what this is, exactly as for a package.
    QFile sfoFile(folder.filePath(QStringLiteral("sce_sys/param.sfo")));
    if (sfoFile.open(QIODevice::ReadOnly)) {
        SfoReader sfo;
        if (sfo.parse(sfoFile.read(4 * 1024 * 1024))) {
            info.titleId = sfo.titleId();
            info.title = sfo.title();
            info.appVersion = sfo.appVersion();
            info.category = sfo.category();
            info.contentId = sfo.string(QStringLiteral("CONTENT_ID"));
        }
        sfoFile.close();
    }

    // Failing that, a folder named like a Title ID is the convention people
    // actually use for an unpacked game.
    if (info.titleId.isEmpty() && looksLikeRetailTitleId(folder.dirName().toUpper()))
        info.titleId = folder.dirName().toUpper();

    if (info.titleId.isEmpty()) {
        info.error = QStringLiteral(
            "No sce_sys/param.sfo, and the folder is not named like a Title ID");
        return info;
    }
    if (info.title.isEmpty())
        info.title = info.titleId;

    // Walk it once for the size and the file count the card will see.
    QDirIterator walker(localPath, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
    int guard = 0;
    while (walker.hasNext() && guard < 200000) {
        walker.next();
        ++guard;
        ++info.entryCount;
        info.unpackedSize += walker.fileInfo().size();
    }
    info.fileSize = info.unpackedSize;

    if (info.entryCount == 0) {
        info.error = QStringLiteral("That folder is empty");
        return info;
    }

    QFile iconFile(folder.filePath(QStringLiteral("sce_sys/icon0.png")));
    if (iconFile.open(QIODevice::ReadOnly)) {
        info.iconPng = iconFile.read(4 * 1024 * 1024);
        iconFile.close();
    }

    info.valid = true;
    return info;
}

bool PackageInspector::looksLikeNoNpDrmDump(const QString &localPath)
{
    const QDir folder(localPath);
    if (!folder.exists())
        return false;

    static const QStringList kRoots = {
        QStringLiteral("app"), QStringLiteral("addcont"), QStringLiteral("license")
    };
    for (const QString &root : kRoots) {
        if (folder.exists(root) && QFileInfo(folder.filePath(root)).isDir())
            return true;
    }
    return false;
}

QVector<PackageInfo> PackageInspector::inspectNoNpDrmDump(const QString &localPath)
{
    QVector<PackageInfo> parts;
    const QDir folder(localPath);

    static const QStringList kRoots = {
        QStringLiteral("app"), QStringLiteral("addcont"), QStringLiteral("license")
    };
    for (const QString &root : kRoots) {
        const QString rootPath = folder.filePath(root);
        if (!QFileInfo(rootPath).isDir())
            continue;
        // Each child is, structurally, exactly what a standalone
        // unpacked-game-folder drop is: a directory that lands at a fixed
        // root under its own name. Reuse that inspection completely rather
        // than duplicating it.
        parts += inspectContentRoot(rootPath, root);
    }
    return parts;
}

bool PackageInspector::looksLikeStandaloneContentFolder(const QString &localPath)
{
    const QDir folder(localPath);
    if (!folder.exists())
        return false;

    static const QStringList kRoots = {
        QStringLiteral("app"), QStringLiteral("addcont"), QStringLiteral("license")
    };
    if (!kRoots.contains(folder.dirName()))
        return false;

    // Must actually hold something to split, or this is just an oddly named
    // empty folder, not this format.
    const QFileInfoList children = folder.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    return !children.isEmpty();
}

QVector<PackageInfo> PackageInspector::inspectStandaloneContentFolder(const QString &localPath)
{
    const QString rootName = QDir(localPath).dirName();
    static const QStringList kRoots = {
        QStringLiteral("app"), QStringLiteral("addcont"), QStringLiteral("license")
    };
    if (!kRoots.contains(rootName))
        return {};
    return inspectContentRoot(localPath, rootName);
}

QString PackageInspector::sha256OfFile(const QString &localPath, qint64 *bytesHashed)
{
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    // Fixed-size chunks so hashing a 4 GB package never spikes memory.
    constexpr qint64 kChunk = 1 << 20;
    QByteArray buffer;
    buffer.resize(kChunk);

    while (!file.atEnd()) {
        const qint64 read = file.read(buffer.data(), kChunk);
        if (read < 0)
            return {};
        hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(read)));
        total += read;
    }

    if (bytesHashed)
        *bytesHashed = total;
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace vsp
