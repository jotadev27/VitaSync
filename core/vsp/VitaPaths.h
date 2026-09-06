#pragma once

#include "PackageInspector.h"

#include <QString>
#include <QVector>

namespace vsp {

/// A named location on the Vita worth putting one click away.
struct VitaLocation
{
    QString id;
    QString label;
    QString path;
    QString iconName;
};

/// The Vita's filesystem conventions, in one place. Nothing else in the code
/// base hard-codes a "ux0:/..." string.
namespace vitapaths {

/// Mount points a Vita may expose over FTP, most likely first.
QVector<VitaLocation> mountPoints();

/// Locations shown in the browser sidebar for \a mount (default "ux0:").
QVector<VitaLocation> quickLocations(const QString &mount = QStringLiteral("ux0:"));

/// Where a package of this kind has to be staged before it can be installed.
/// Games/homebrew/themes go to the VPK staging folder VitaShell installs from.
QString stagingDirFor(PackageKind kind, const QString &mount = QStringLiteral("ux0:"));

/// Where a media file should land so the Vita's own apps pick it up.
/// Returns an empty string for kinds that are not media.
QString mediaDirFor(PackageKind kind, const QString &mount = QStringLiteral("ux0:"));

/// The single destination for any dropped file, media or package.
QString destinationDirFor(PackageKind kind, const QString &mount = QStringLiteral("ux0:"));

/// Maps one of "app", "addcont", "license" to its fixed destination folder --
/// what a NoNpDrm-style dump's parts are routed by instead of `PackageKind`,
/// since one dropped folder can contain parts for more than one of these at
/// once. Returns an empty string for anything else.
QString fixedRootFor(const QString &name, const QString &mount = QStringLiteral("ux0:"));

QString savedataDir(const QString &mount = QStringLiteral("ux0:"));
QString photoDir(const QString &mount = QStringLiteral("ux0:"));
QString videoDir(const QString &mount = QStringLiteral("ux0:"));
QString vpkDir(const QString &mount = QStringLiteral("ux0:"));

/// Where installed applications live. An already-unpacked game goes to
/// `app/<TITLE ID>/` here and is registered by VitaShell's "Refresh LiveArea".
QString appDir(const QString &mount = QStringLiteral("ux0:"));

/// Where add-on content (DLC) lives once installed, and one of the fixed
/// destinations a NoNpDrm-style dump is split across. See PackageInspector's
/// `noNpDrmRoot`.
QString addContDir(const QString &mount = QStringLiteral("ux0:"));

/// Where license files (.rif) live once installed, and the third fixed
/// destination a NoNpDrm-style dump may be split across.
QString licenseDir(const QString &mount = QStringLiteral("ux0:"));

/// Where installed PS Vita themes live once the system owns them.
QString themeDir(const QString &mount = QStringLiteral("ux0:"));

/// Staging folder for PS Vita home-screen themes: a folder with `theme.xml`
/// goes here and is applied on the device with Custom Themes Manager.
QString customThemeDir(const QString &mount = QStringLiteral("ux0:"));

/// VitaShell's own skin folder. A skin dropped here is selected from inside
/// VitaShell (START, then left/right, then Restart VitaShell).
QString shellThemeDir(const QString &mount = QStringLiteral("ux0:"));

/// The file VitaShell reads to decide which skin is active.
QString shellThemeSettingFile(const QString &mount = QStringLiteral("ux0:"));

/// True when \a path is one of the folders the Vita firmware itself owns and
/// where a careless delete does real damage. The UI uses this to ask twice.
bool isProtectedPath(const QString &remotePath);

} // namespace vitapaths
} // namespace vsp
