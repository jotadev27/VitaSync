#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace vsp {

class ZipReader;

/// Which of the two unrelated things called a "theme" on a hacked Vita this is.
///
/// They install to different places and are finished by different tools, so the
/// app must not conflate them:
///
///  * SystemTheme  -- a PS Vita home-screen theme. A folder with `theme.xml` at
///                    its root, staged in `ux0:/customtheme/<folder>/` and
///                    applied on the device with Custom Themes Manager.
///  * ShellTheme   -- a VitaShell skin. A folder with `colors.txt` and/or the
///                    documented PNG set, staged in
///                    `ux0:VitaShell/theme/<folder>/` and selected inside
///                    VitaShell itself.
enum class ThemeFlavour {
    None,
    SystemTheme,
    ShellTheme
};

/// What a theme archive says about itself.
struct ThemeInfo
{
    ThemeFlavour flavour = ThemeFlavour::None;
    bool valid = false;
    QString error;

    /// Folder name the theme must end up in on the device.
    QString folderName;
    /// Path prefix inside the archive that folder starts at ("" = archive root).
    QString archivePrefix;

    QString title;           ///< InfomationProperty/m_title/m_default
    QString provider;        ///< InfomationProperty/m_provider/m_default
    QString contentVersion;  ///< InfomationProperty/m_contentVer, "01.00"
    QString formatVersion;   ///< the <theme format-ver="..."> attribute

    QString previewFile;     ///< best thumbnail path inside the archive
    QByteArray previewPng;   ///< the thumbnail itself, when it could be read

    /// A couple of colours the theme declares, as "#RRGGBB". Used to draw a
    /// preview that still says something about the theme when it ships no
    /// image of its own.
    QStringList accentColors;

    int fileCount = 0;
    qint64 unpackedSize = 0;

    QString displayName() const;
};

/// Reads the two theme formats out of an archive. No network, no writes.
class ThemeReader
{
public:
    /// Parses a `theme.xml` blob. Tolerant of missing fields -- a theme with no
    /// title is still a valid theme, it just gets named after its folder.
    static ThemeInfo parseThemeXml(const QByteArray &xml);

    /// Works out whether \a archive holds a theme, of which kind, and where in
    /// the archive it starts. Fills in metadata and the preview image.
    static ThemeInfo inspectArchive(ZipReader *archive, const QString &fallbackName);

    /// The file names VitaShell documents for its skins. Used to recognise a
    /// shell theme that ships without a colors.txt.
    static const QStringList &shellThemeFileNames();
};

} // namespace vsp
