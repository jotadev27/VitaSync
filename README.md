<p align="center">
  <img src="assets/logo/appicon_256.png" alt="VitaSync" width="112" height="112">
</p>
<h1 align="center">VitaSync</h1>
<p align="center"><strong>Your PC. Your PS Vita. Your files.</strong></p>
<p align="center">Drop it. Check the destination. Press Start.</p>
<p align="center">
  <a href="https://github.com/jotadev27/VitaSync/releases"><img src="https://img.shields.io/badge/DOWNLOAD-v3.0-0070D1?style=for-the-badge&amp;labelColor=39424C" alt="Download VitaSync 3.0" height="28"></a>
  <a href="#quick-start"><img src="https://img.shields.io/badge/READ-QUICK_START-202A35?style=for-the-badge&amp;labelColor=39424C" alt="Read the quick start guide" height="28"></a>
  <a href="docs/usb.md"><img src="https://img.shields.io/badge/GUIDE-USB_CABLE-202A35?style=for-the-badge&amp;labelColor=39424C" alt="Read the USB cable guide" height="28"></a>
</p>
<p align="center"><sub>Windows 10/11 x64 · Wi-Fi / FTP or USB · No account required</sub></p>

Games, savedata, photos, music, and videos—one place to send them to your Vita.
VitaSync connects your PC to [VitaShell](https://github.com/TheOfficialFloW/VitaShell)
over Wi-Fi or a USB cable. Review where each item will go, browse your storage,
and follow transfers from the same window.

No telemetry, cloud service, or automatic update checks. Connection settings stay
on your computer. A homebrew-enabled PS Vita running VitaShell is required.

## Quick start

**Choose your Windows download:**

| Installer | Portable |
| --- | --- |
| Run `Setup.exe` and follow the wizard. | Extract `Portable.rar` completely, then open `VitaSync/vitasync.exe`. |
| Includes an uninstaller and an optional desktop shortcut. | Keep the bundled libraries and folders beside the executable. |

**Connect, then send:**

1. In VitaShell, press **START** and choose **FTP** or **USB** for the SELECT button.
   Close the menu, then press **SELECT** to start the connection.
2. In VitaSync, choose the same connection mode. For FTP, enter the address and
   port shown on the Vita; for USB, select the exported drive and connect.
3. Drop files or folders into VitaSync, review their destinations, and press
   **Start**. Use **Transfers** to follow progress.

FTP is unencrypted; use a trusted local network. Safely eject USB storage when
finished. Uploading a VPK does not install it: open it in VitaShell to install.
Media is copied without conversion, and Android APKs cannot run on a PS Vita.

<details>
<summary>Verify your download</summary>

Compare the downloaded file with `SHA256SUMS.txt` from the same release:

```powershell
Get-FileHash .\VitaSync-3.0-Windows-x64-Setup.exe -Algorithm SHA256
```

Windows packages are not code-signed. See each release's notes for changes,
archive compatibility, and verification limits.

</details>

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

## For developers

<details>
<summary>Build, test, and package VitaSync</summary>

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

</details>

---

<p align="center">Made for the Vita homebrew community. Maintained by <a href="https://github.com/jotadev27">jotadev27</a>.</p>
