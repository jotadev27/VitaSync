# What a "PS Vita theme" actually is

The brief asked that this be checked rather than assumed, and it was worth
checking: **there are two unrelated things called a theme on a hacked Vita**,
they use different formats, install to different folders, and are applied by
different tools. Conflating them would put files where nothing reads them.

Neither is a `.pkg` (that is the PS3 `.p3t` / PSN packaging world), and neither
is a `.vpk` — a VPK installs an *application*, which is what Custom Themes
Manager itself ships as.

## 1. PS Vita home-screen theme

A folder whose root contains **`theme.xml`**, alongside its images and
optionally an `.at9` background track.

```
Neon_Drift/
  theme.xml                  <- the manifest, must be at the folder root
  br.png                     <- home background
  lockpaper.png              <- lock screen
  preview_thumbnail.png      <- the thumbnail theme.xml points at
  preview_livearea.png
  icon_settings.png  icon_photos.png  icon_music.png  ...
```

Verified against a real published theme
([c2t-r/PSVita_CustomTheme_Hu-Tao](https://github.com/c2t-r/PSVita_CustomTheme_Hu-Tao)),
whose layout is exactly this.

**Destination:** `ux0:/customtheme/<folder>/`

**Applied by:** Custom Themes Manager on the device ("install from local
folder"). It needs unsafe homebrew enabled, because it writes to the Vita's
application database at `ur0:shell/db/app.db`. Nothing over FTP can do that.

**Requirement:** `theme.xml` must sit at the root of the theme folder, not in a
subfolder, or the manager will not see it.

### theme.xml

```xml
<theme format-ver="01.00" package="0">
  <HomeProperty>…</HomeProperty>
  <InfomationBarProperty>…</InfomationBarProperty>
  <InfomationProperty>
    <m_provider><m_default>kaido</m_default>…</m_provider>
    <m_contentVer>01.00</m_contentVer>
    <m_title><m_default>Neon Drift</m_default>…</m_title>
    <m_packageImageFilePath>preview_thumbnail.png</m_packageImageFilePath>
  </InfomationProperty>
  <StartScreenProperty>…</StartScreenProperty>
</theme>
```

Note the trap, and note that Sony misspelled "Information": `m_default` appears
under **both** `m_title` and `m_provider`. A parser that simply looks for the
first `m_default` gets the author where the name should be. `ThemeReader` keys
on the full element path, and there is a test that fails if that regresses.

The app reads the title, provider, content version and the declared preview
image straight out of the archive, so a dropped theme shows its real name and
its own artwork before anything is sent.

## 2. VitaShell skin

A folder of `colors.txt` plus any of VitaShell's documented image files. Every
file is optional; missing ones fall back to the built-in default.

```
Midnight/
  colors.txt          <- all colours adjustable
  wallpaper.png       bg_browser.png      bg_audioplayer.png
  folder_icon.png     file_icon.png       archive_icon.png
  battery.png         play.png            pause.png       …
  font.pgf            <- optional custom font
```

**Destination:** `ux0:VitaShell/theme/<folder>/`

**Applied by:** VitaShell itself — press START, then left/right to pick a
theme, then "Restart VitaShell".

**Selection file:** `ux0:VitaShell/theme/theme.txt` holds
`THEME_NAME = "YOUR_THEME_NAME"`. The app does **not** write this file: doing so
would silently change the user's active skin as a side effect of copying one,
and the choice belongs to them.

Verified directly against VitaShell's source, which builds these paths as
`ux0:VitaShell/theme/%s/colors.txt`, `ux0:VitaShell/theme/%s/font.pgf` and
`ux0:VitaShell/theme/theme.txt`.

## How the app tells them apart

`ThemeReader::inspectArchive` decides in this order:

1. An entry named `theme.xml` at the shallowest depth → **home-screen theme**.
   The folder it sits in is the folder name to use on the device.
2. Otherwise `colors.txt`, or at least three of VitaShell's documented file
   names sharing one folder → **VitaShell skin**.
3. Otherwise it is not a theme, and the file falls through to the normal
   package or media handling.

The three-file threshold on rule 2 exists so that a zip holding a couple of
stray images called `play.png` and `pause.png` is not promoted to a skin.
There is a test for exactly that.

## How installing works

A theme is a directory, so it cannot be sent the way a VPK is:

1. The archive is unpacked into a temporary folder. This is the only place the
   app writes archive contents to disk, so it is where zip-slip is stopped —
   every entry name is rebuilt from sanitised components and the result is
   confirmed to be inside the destination before anything is opened.
2. Every parent directory is created on the device, shallowest first, because
   the Vita's `MKD` does not create parents.
3. Each file is uploaded in turn. The queue reports the whole thing as one job
   with a file counter (`7 / 13 files`).
4. The temporary folder is removed, and the job parks in "Confirm on Vita"
   showing which tool applies it — Custom Themes Manager or VitaShell.

The app never claims to have *applied* a theme, because it cannot. Same
principle as installing a VPK: see [install-trigger.md](install-trigger.md).

## Sources

- [VitaShell — customisation and theme paths](https://github.com/TheOfficialFloW/VitaShell/blob/master/README.md)
- [VitaShell — theme.c, the paths in code](https://github.com/TheOfficialFloW/VitaShell/blob/master/theme.c)
- [Custom Themes Manager (Red Squirrel)](https://redsquirrel87.com/custom-themes-manager)
- [Custom Themes Manager — GameBrew](https://www.gamebrew.org/wiki/Custom_Themes_Manager_Vita)
- [Creating Custom Themes — ConsoleMods Wiki](https://consolemods.org/wiki/Vita:Creating_Custom_Themes)
- [A real published theme, for layout](https://github.com/c2t-r/PSVita_CustomTheme_Hu-Tao)
