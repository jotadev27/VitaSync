<p align="center">
  <img src="assets/logo/appicon_256.png" alt="VitaSync" width="112" height="112">
</p>
<h1 align="center">VitaSync</h1>
<p align="center">Transfer files between your PC and PS Vita over Wi-Fi or USB.</p>
<p align="center"><strong>Windows 10/11 x64 · Version 3.0</strong></p>
<p align="center">
  <a href="https://github.com/jotadev27/VitaSync/releases">Downloads and release notes</a>
  · <a href="docs/usb.md">USB guide</a>
</p>

VitaSync works with [VitaShell](https://github.com/TheOfficialFloW/VitaShell)
on a homebrew-enabled PS Vita. No account, telemetry, cloud service, or automatic
update checks. Your connection settings stay on your computer.

## Get started

1. Download the **Setup.exe** installer or **Portable.rar** archive from Releases.
   Run the installer, or extract the portable archive completely and open
   `VitaSync/vitasync.exe`. Keep its libraries and folders together.
2. In VitaShell, press **START** and choose **FTP** or **USB** for the SELECT button.
   Close the menu, then press **SELECT** to start the connection.
3. In VitaSync, choose the same connection mode. For FTP, enter the address and
   port shown on the Vita; for USB, select the exported drive and connect.
4. Drop files or folders into VitaSync, review their destinations, and press
   **Start**. Use **Transfers** to follow progress.

FTP is unencrypted; use a trusted local network. Safely eject USB storage when
finished. Uploading a VPK does not install it: open it in VitaShell to install.
Media is copied without conversion, and Android APKs cannot run on a PS Vita.

Check downloaded files against `SHA256SUMS.txt`, for example:

```powershell
Get-FileHash .\VitaSync-3.0-Windows-x64-Setup.exe -Algorithm SHA256
```

Windows packages are not code-signed. See each release's notes for changes,
archive compatibility, and verification limits.

## Screenshots

<p align="center"><strong>Connect over Wi-Fi or USB</strong></p>
<p align="center">
  <img src="docs/images/link.png" alt="VitaSync connection screen in Light mode" width="960">
</p>

<p align="center"><strong>Drop files, review destinations, and press Start</strong></p>
<p align="center">
  <img src="docs/images/install.png" alt="Send files with generated photo, music, video, and data samples" width="960">
</p>

<p align="center"><strong>Browse files on your Vita</strong></p>
<p align="center">
  <img src="docs/images/browse.png" alt="VitaSync browsing a generated sample card" width="960">
</p>

<p align="center"><strong>Follow transfers in Light or Dark mode</strong></p>
<p align="center">
  <img src="docs/images/transfers.png" alt="Completed sample transfer in Dark mode" width="960">
</p>

Screenshots use generated samples and a local test server, with no personal files.

## Build from source

Requires CMake 3.21+, C++17, Qt 6.5+ (Core, Gui, Network, Concurrent, Quick,
QuickControls2, Svg; Test when enabled), and zlib. Qt Multimedia is optional.
Match your compiler to the Qt kit; the Windows release uses Qt 6.8.3 and MinGW.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The tests use synthetic local storage and a mock FTP server. They do not replace
physical Vita checks. See release notes for the verification status of this build.

Windows packaging uses the existing Release executable, NSIS and a signed RAR tool:

```powershell
.\tools\package_windows.ps1 -BuildDir build `
  -QtDir C:\Qt\6.8.3\mingw_64 `
  -MinGWDir C:\Qt\Tools\mingw1310_64 `
  -NsisExe 'C:\Program Files (x86)\NSIS\makensis.exe' `
  -RarExe 'C:\Program Files\WinRAR\Rar.exe'
```

The four release files go in `installer/`, which Git ignores. Runtime staging stays
in the build directory. Packaging uses local license files and downloads nothing.
Linux source-build and packaging scripts are available in `tools/`.

Maintained by [jotadev27](https://github.com/jotadev27).
