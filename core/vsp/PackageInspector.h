#pragma once

#include "ThemeReader.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vsp {

enum class PackageKind {
    Unknown,
    Game,        ///< CATEGORY "gd" -- a full application
    Patch,       ///< CATEGORY "gp"
    AddOn,       ///< CATEGORY "ac"
    Homebrew,    ///< a VPK whose param.sfo does not look like a retail dump
    FolderGame,  ///< an already-unpacked game: a folder named by its Title ID
    SystemTheme, ///< PS Vita home-screen theme: a theme.xml folder
    ShellTheme,  ///< VitaShell skin: a colors.txt / PNG folder
    Video,
    Photo,
    Music
};

QString packageKindLabel(PackageKind kind);

/// Everything we can learn about a dropped file without touching the network.
struct PackageInfo
{
    bool valid = false;
    QString error;

    PackageKind kind = PackageKind::Unknown;
    QString localPath;
    QString fileName;
    qint64 fileSize = 0;

    QString titleId;          ///< from param.sfo, never from the file name
    QString title;
    QString appVersion;
    QString category;
    QString contentId;

    QByteArray iconPng;       ///< sce_sys/icon0.png, or a theme's own preview

    // Themes only. A theme is a folder of files rather than one installable
    // blob, so the app has to know where inside the archive it starts and how
    // much is going to come out of it.
    ThemeFlavour themeFlavour = ThemeFlavour::None;
    QString themeFolderName;  ///< folder the theme must land in on the device
    QString archivePrefix;    ///< where the theme starts inside the archive
    QString provider;         ///< theme author, from theme.xml
    int entryCount = 0;
    qint64 unpackedSize = 0;
    /// True when the dropped thing is a folder rather than an archive, so the
    /// installer walks it instead of unpacking it.
    bool localIsDirectory = false;
    QStringList accentColors;  ///< colours the theme declares, for its preview

    /// Filled in by MetadataDb, not by the inspector itself.
    bool metadataMatched = false;
    QString coverPath;
    QString publisher;
    QString region;

    /// Set only for one part of a NoNpDrm-style dump (a parent folder holding
    /// "app"/"addcont"/"license" subfolders, each with a serial-numbered
    /// folder inside): which of those three this part came from, so it can
    /// be routed to that fixed destination root instead of the usual
    /// kind-based lookup. Empty for everything else. See
    /// PackageInspector::inspectNoNpDrmDump() and docs/nonpdrm.md.
    QString noNpDrmRoot;

    /// A display name that is always safe to show, even for unmatched files.
    QString displayName() const;
    bool isInstallable() const;
    /// True when installing means sending a folder rather than a single file.
    bool isDirectoryPayload() const;
};

/// Reads a local file and works out what it is. Pure local work -- no network,
/// no writes, and the archive is only ever read in bounded chunks.
class PackageInspector
{
public:
    /// Inspects \a localPath. Recognises VPK/ZIP packages (games, homebrew,
    /// themes), already-unpacked game folders, and media files by extension.
    static PackageInfo inspect(const QString &localPath);

    /// Inspects an unpacked game folder: a directory holding `sce_sys` and
    /// named by, or declaring, a Title ID.
    static PackageInfo inspectFolder(const QString &localPath);

    /// True when \a localPath directly contains one or more of exactly
    /// "app", "addcont", "license" as top-level subfolders -- the shape a
    /// NoNpDrm-style dump has, as opposed to a single game folder (which has
    /// `sce_sys` etc. directly inside it, not one of these three names).
    static bool looksLikeNoNpDrmDump(const QString &localPath);

    /// Splits a NoNpDrm-style dump into one PackageInfo per serial-numbered
    /// folder found under each present "app"/"addcont"/"license" subfolder,
    /// each inspected with inspectFolder() (the exact logic a standalone
    /// game-folder drop uses) and tagged with which of the three it came
    /// from via PackageInfo::noNpDrmRoot. Empty if \a localPath does not
    /// look like this format at all.
    static QVector<PackageInfo> inspectNoNpDrmDump(const QString &localPath);

    /// True when \a localPath is itself named exactly "app", "addcont" or
    /// "license" and holds at least one subfolder -- the same shape one of
    /// those three has *inside* a NoNpDrm-style dump, just dragged on its
    /// own instead of as part of the parent folder. Without this, a
    /// standalone "app" or "addcont" folder either gets refused outright or
    /// -- if its inner serial folder happens to be dragged directly instead
    /// -- silently defaults to ux0:app regardless of which one it actually
    /// was, corrupting/misplacing add-on content on the console.
    static bool looksLikeStandaloneContentFolder(const QString &localPath);

    /// Splits a standalone "app"/"addcont"/"license" folder the same way
    /// inspectNoNpDrmDump() splits one found inside a dump: one PackageInfo
    /// per immediate subfolder, tagged with PackageInfo::noNpDrmRoot set to
    /// \a localPath's own name. Empty if \a localPath does not look like
    /// this format at all.
    static QVector<PackageInfo> inspectStandaloneContentFolder(const QString &localPath);

    /// True when \a id has the shape of a Vita Title ID (ABCD12345).
    static bool looksLikeTitleId(const QString &id);

    /// Streaming SHA-256 of a local file, used to verify a transfer landed
    /// intact before anything gets installed. Returns an empty string on error.
    static QString sha256OfFile(const QString &localPath,
                                qint64 *bytesHashed = nullptr);

    /// Extension-only classification, used for media drops and for deciding
    /// whether a file is worth opening as an archive at all.
    static PackageKind kindForExtension(const QString &fileName);
};

} // namespace vsp
