#include "PackageInspector.h"

#include "SfoReader.h"
#include "ThemeReader.h"
#include "ZipReader.h"
#include "ArchiveExtractor.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

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

bool mountWrapper(QString name)
{
    if (name.endsWith(QLatin1Char(':'))) name.chop(1);
    return QStringList{QStringLiteral("ux0"), QStringLiteral("uma0"), QStringLiteral("ur0"),
                       QStringLiteral("imc0"), QStringLiteral("grw0")}.contains(name.toLower());
}

QString declaredContentRoot(const QString &localPath, bool *merge)
{
    const QFileInfo file(localPath);
    QStringList parts = QDir::fromNativeSeparators(file.absoluteFilePath())
                            .split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const bool directory = file.isDir();
    *merge = false;
    if (directory && mountWrapper(parts.constLast())) { *merge = true; return QStringLiteral("/"); }
    for (int i = parts.size() - 2; i >= 0; --i) {
        if (!mountWrapper(parts.at(i))) continue;
        QStringList relative = parts.mid(i + 1);
        if (directory && relative.size() == 1) *merge = true;
        else relative.removeLast();
        return relative.isEmpty() ? QStringLiteral("/") : relative.join(QLatin1Char('/'));
    }
    if (directory && parts.constLast().compare("user", Qt::CaseInsensitive) == 0
        && QFileInfo(QDir(localPath).filePath("00/savedata")).isDir()) {
        *merge = true; return QStringLiteral("user");
    }
    for (int i = 0; i < parts.size(); ++i) {
        const QString name = parts.at(i).toLower();
        if (name != QLatin1String("data") && name != QLatin1String("savedata")
            && name != QLatin1String("user")) continue;
        // user/<account>/savedata is explicit. A random folder named "user"
        // alone does not establish a Vita layout.
        if (name == QLatin1String("user") &&
            !(i + 2 < parts.size() && parts.at(i + 2).compare("savedata", Qt::CaseInsensitive) == 0))
            continue;
        QStringList relative = parts.mid(i);
        if (name == QLatin1String("savedata")) {
            relative.prepend(QStringLiteral("00"));
            relative.prepend(QStringLiteral("user"));
        }
        if (directory && i == parts.size() - 1) *merge = true;
        else relative.removeLast();
        return relative.join(QLatin1Char('/'));
    }
    return {};
}

void applyContentLayout(PackageInfo *info)
{
    bool merge = false;
    const QString declared = declaredContentRoot(info->localPath, &merge);
    if (declared.isEmpty()) return;
    info->contentRoot = declared;
    info->mergeContents = merge;
    if (!info->contentRoot.isEmpty() && info->localIsDirectory) {
        info->kind = PackageKind::Folder;
        info->title = info->fileName;
        info->titleId.clear();
        info->themeFolderName.clear();
    }
}

void inspectArchiveContents(PackageInfo *info, const QStringList &entries)
{
    QStringList files;
    for (QString entry : entries) {
        entry.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (entry.endsWith(QLatin1Char('/'))) continue;
        const QStringList parts = entry.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.contains(QStringLiteral("..")) || parts.contains(QStringLiteral("."))
            || entry.startsWith(QLatin1Char('/'))) {
            info->error = QStringLiteral("Archive contains unsafe paths"); return;
        }
        if (!entry.isEmpty()) files.append(entry);
    }
    if (files.isEmpty() || files.size() > 20000) {
        info->error = QStringLiteral("Archive is empty or contains too many files"); return;
    }
    info->kind = PackageKind::Archive;
    info->mergeContents = true;
    info->contentRoot = QStringLiteral("/");
    info->autoRouteContents = true;
    info->entryCount = files.size();
    QString prefix;
    for (int depth = 0; depth < 12; ++depth) {
        QSet<QString> roots;
        for (const QString &file : files) roots.insert(file.mid(prefix.size()).section('/', 0, 0));
        QString wrapper;
        for (const QString &root : roots) {
            if (mountWrapper(root)) {
                if (!wrapper.isEmpty()) {
                    info->error = QStringLiteral("Archive contains multiple Vita partitions; add one partition at a time"); return;
                }
                wrapper = root;
            }
        }
        if (!wrapper.isEmpty()) {
            info->archivePrefix = prefix + wrapper + QLatin1Char('/');
            info->autoRouteContents = false;
            break;
        }
        QSet<QString> lowerRoots;
        for (const QString &root : roots) lowerRoots.insert(root.toLower());
        if (lowerRoots.contains(QStringLiteral("data")) || lowerRoots.contains(QStringLiteral("user"))
            || lowerRoots.contains(QStringLiteral("app")) || lowerRoots.contains(QStringLiteral("addcont"))
            || lowerRoots.contains(QStringLiteral("license")) || lowerRoots.contains(QStringLiteral("video"))
            || lowerRoots.contains(QStringLiteral("picture")) || lowerRoots.contains(QStringLiteral("music"))) {
            info->archivePrefix = prefix;
            info->autoRouteContents = false;
            break;
        }
        if (roots.size() == 1 && roots.cbegin()->compare("savedata", Qt::CaseInsensitive) == 0) {
            info->archivePrefix = prefix + *roots.cbegin() + QLatin1Char('/');
            info->contentRoot = QStringLiteral("user/00/savedata");
            info->autoRouteContents = false;
            break;
        }
        if (roots.size() != 1) break;
        const QString next = prefix + *roots.cbegin() + QLatin1Char('/');
        bool allInside = true;
        for (const QString &file : files) allInside &= file.startsWith(next);
        if (!allInside) break;
        prefix = next;
    }
    info->valid = true;
    info->error.clear();
    if (info->autoRouteContents) {
        bool merge = false;
        const QString declared = declaredContentRoot(info->localPath, &merge);
        if (!declared.isEmpty()) {
            info->contentRoot = declared;
            info->autoRouteContents = false;
        }
    }
}

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
    case PackageKind::Folder:   return QStringLiteral("Folder");
    case PackageKind::Archive:  return QStringLiteral("Archive");
    case PackageKind::AndroidApp: return QStringLiteral("Android APK");
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
           || kind == PackageKind::FolderGame || kind == PackageKind::Folder
           || kind == PackageKind::Archive;
}

bool PackageInfo::isInstallable() const
{
    if (!contentRoot.isEmpty()) return false;
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
    if (suffix == QLatin1String("apk"))
        return PackageKind::AndroidApp;
    if (QStringList { QStringLiteral("rar"), QStringLiteral("7z"),
                      QStringLiteral("tar"), QStringLiteral("gz"),
                      QStringLiteral("bz2"), QStringLiteral("xz") }.contains(suffix))
        return PackageKind::Archive;
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

    if (fileInfo.isDir()) {
        info = inspectFolder(localPath);
        applyContentLayout(&info);
        return info;
    }

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
    if (byExtension == PackageKind::Archive) {
        QString error;
        const QStringList entries = ArchiveExtractor::entryNames(localPath, &error);
        if (!error.isEmpty()) { info.error = error; return info; }
        inspectArchiveContents(&info, entries);
        return info;
    }
    applyContentLayout(&info);
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
            if (suffix == QLatin1String("zip") && info.category == QLatin1String("sd")) {
                // Savedata is a tree to extract and route, never an installable
                // application. The upload planner reads its SFO destination.
                inspectArchiveContents(&info, archive.entryNames());
                return info;
            }
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
        } else if (info.error.isEmpty() && suffix == QLatin1String("zip")) {
            inspectArchiveContents(&info, archive.entryNames());
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

bool PackageInspector::hasContentLayout(const QString &localPath)
{
    bool merge = false;
    return !declaredContentRoot(localPath, &merge).isEmpty();
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

    if (info.category == QLatin1String("sd")) {
        info.kind = PackageKind::Folder;
        info.contentRoot = QStringLiteral("user/00/savedata");
        info.title = info.fileName;
    }

    if (info.titleId.isEmpty()) {
        info.kind = PackageKind::Folder;
        info.title = info.fileName;
        QFile themeXml(folder.filePath(QStringLiteral("theme.xml")));
        if (themeXml.exists()) {
            if (!themeXml.open(QIODevice::ReadOnly)) {
                info.error = QStringLiteral("Cannot read theme.xml");
                return info;
            }
            const ThemeInfo theme = ThemeReader::parseThemeXml(themeXml.read(1024 * 1024));
            if (!theme.valid) {
                info.error = theme.error;
                return info;
            }
            info.kind = PackageKind::SystemTheme;
            info.themeFlavour = ThemeFlavour::SystemTheme;
            if (!theme.title.isEmpty()) info.title = theme.title;
            info.provider = theme.provider;
            info.appVersion = theme.contentVersion;
        } else if (QFileInfo(folder.filePath(QStringLiteral("colors.txt"))).isFile()) {
            info.kind = PackageKind::ShellTheme;
            info.themeFlavour = ThemeFlavour::ShellTheme;
        }
        if (info.kind == PackageKind::SystemTheme || info.kind == PackageKind::ShellTheme)
            info.themeFolderName = info.fileName;
    }
    if (info.title.isEmpty())
        info.title = info.titleId;

    // Walk it once for the size and the file count the card will see.
    QDirIterator walker(localPath, QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks,
                        QDirIterator::Subdirectories);
    int guard = 0;
    while (walker.hasNext() && guard < 200000) {
        walker.next();
        ++guard;
        ++info.entryCount;
        info.unpackedSize += walker.fileInfo().size();
    }
    if (walker.hasNext()) {
        info.error = QStringLiteral("Folder contains too many files to inspect");
        return info;
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
