#include "VitaPaths.h"
#include "PathUtils.h"

namespace vsp::vitapaths {
namespace {

QString under(const QString &mount, const QString &sub)
{
    return path::normalizeRemote(QStringLiteral("/%1/%2").arg(mount, sub));
}

} // namespace

QVector<VitaLocation> mountPoints()
{
    return {
        { QStringLiteral("ux0"),  QStringLiteral("Memory Card"),   QStringLiteral("/ux0:"),  QStringLiteral("card") },
        { QStringLiteral("ur0"),  QStringLiteral("Internal"),      QStringLiteral("/ur0:"),  QStringLiteral("chip") },
        { QStringLiteral("uma0"), QStringLiteral("USB / SD2Vita"), QStringLiteral("/uma0:"), QStringLiteral("usb") },
        { QStringLiteral("imc0"), QStringLiteral("Internal Card"), QStringLiteral("/imc0:"), QStringLiteral("chip") },
        { QStringLiteral("grw0"), QStringLiteral("Game Cart"),     QStringLiteral("/grw0:"), QStringLiteral("cart") }
    };
}

QVector<VitaLocation> quickLocations(const QString &mount)
{
    return {
        // The true filesystem root, not this mount's own root: that is where
        // the device answers with the list of every partition it actually
        // has (ux0:, ur0:, uma0:, imc0:, grw0: -- whichever are really
        // present), which is what "Root" is for. See docs/browsing.md.
        { QStringLiteral("root"),     QStringLiteral("Root"),     QStringLiteral("/"), QStringLiteral("grid") },
        { QStringLiteral("vpk"),      QStringLiteral("Packages"), vpkDir(mount),               QStringLiteral("package") },
        { QStringLiteral("app"),      QStringLiteral("Apps"),     under(mount, QStringLiteral("app")),     QStringLiteral("grid") },
        { QStringLiteral("video"),    QStringLiteral("Video"),    videoDir(mount),             QStringLiteral("video") },
        { QStringLiteral("picture"),  QStringLiteral("Photos"),   photoDir(mount),             QStringLiteral("photo") },
        { QStringLiteral("music"),    QStringLiteral("Music"),    under(mount, QStringLiteral("music")),   QStringLiteral("music") },
        { QStringLiteral("savedata"), QStringLiteral("Saves"),    savedataDir(mount),          QStringLiteral("save") },
        { QStringLiteral("customtheme"), QStringLiteral("Themes"), customThemeDir(mount),   QStringLiteral("theme") },
        { QStringLiteral("shelltheme"),  QStringLiteral("Skins"),  shellThemeDir(mount),    QStringLiteral("shell") },
        { QStringLiteral("data"),     QStringLiteral("Data"),     under(mount, QStringLiteral("data")),    QStringLiteral("folder") },
        { QStringLiteral("download"), QStringLiteral("Downloads"),under(mount, QStringLiteral("downloads")), QStringLiteral("download") }
    };
}

QString vpkDir(const QString &mount)      { return under(mount, QStringLiteral("vpk")); }
QString appDir(const QString &mount)      { return under(mount, QStringLiteral("app")); }
QString addContDir(const QString &mount)  { return under(mount, QStringLiteral("addcont")); }
QString licenseDir(const QString &mount)  { return under(mount, QStringLiteral("license")); }
QString videoDir(const QString &mount)    { return under(mount, QStringLiteral("video")); }
QString photoDir(const QString &mount)    { return under(mount, QStringLiteral("picture")); }
QString themeDir(const QString &mount)    { return under(mount, QStringLiteral("theme")); }
QString customThemeDir(const QString &mount) { return under(mount, QStringLiteral("customtheme")); }
QString shellThemeDir(const QString &mount)  { return under(mount, QStringLiteral("VitaShell/theme")); }

QString shellThemeSettingFile(const QString &mount)
{
    return under(mount, QStringLiteral("VitaShell/theme/theme.txt"));
}
QString savedataDir(const QString &mount) { return under(mount, QStringLiteral("user/00/savedata")); }

QString stagingDirFor(PackageKind kind, const QString &mount)
{
    switch (kind) {
    case PackageKind::Game:
    case PackageKind::Patch:
    case PackageKind::AddOn:
    case PackageKind::Homebrew:
        return vpkDir(mount);
    case PackageKind::FolderGame:
        // An unpacked game is already in its installed shape; it belongs where
        // installed games live, under its own Title ID.
        return appDir(mount);
    case PackageKind::SystemTheme:
        // Verified against Custom Themes Manager: a home-screen theme is a
        // folder holding theme.xml, and it is picked up from here.
        return customThemeDir(mount);
    case PackageKind::ShellTheme:
        // Verified against VitaShell's own source, which reads skins from
        // ux0:VitaShell/theme/<name>/.
        return shellThemeDir(mount);
    default:
        return {};
    }
}

QString mediaDirFor(PackageKind kind, const QString &mount)
{
    switch (kind) {
    case PackageKind::Video: return videoDir(mount);
    case PackageKind::Photo: return photoDir(mount);
    case PackageKind::Music: return under(mount, QStringLiteral("music"));
    default:                 return {};
    }
}

QString destinationDirFor(PackageKind kind, const QString &mount)
{
    const QString staging = stagingDirFor(kind, mount);
    if (!staging.isEmpty())
        return staging;
    const QString media = mediaDirFor(kind, mount);
    if (!media.isEmpty())
        return media;
    return under(mount, QStringLiteral("downloads"));
}

QString fixedRootFor(const QString &name, const QString &mount)
{
    if (name == QLatin1String("app"))
        return appDir(mount);
    if (name == QLatin1String("addcont"))
        return addContDir(mount);
    if (name == QLatin1String("license"))
        return licenseDir(mount);
    return {};
}

bool isProtectedPath(const QString &remotePath)
{
    const QString normalized = path::normalizeRemote(remotePath);
    if (path::isMountRoot(normalized))
        return true;

    static const QStringList kGuarded = {
        QStringLiteral("app"),      QStringLiteral("appmeta"), QStringLiteral("license"),
        QStringLiteral("patch"),    QStringLiteral("addcont"), QStringLiteral("user"),
        QStringLiteral("tai"),      QStringLiteral("bgdl"),    QStringLiteral("id.dat"),
        QStringLiteral("psm"),      QStringLiteral("pspemu")
    };

    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    // Guard the top-level firmware folders themselves, not their contents:
    // "/ux0:/app" is protected, "/ux0:/app/PCSE00001" is the user's business.
    return parts.size() == 2 && kGuarded.contains(parts.at(1), Qt::CaseInsensitive);
}

} // namespace vsp::vitapaths
