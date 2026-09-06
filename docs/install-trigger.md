# How a VPK actually gets installed

CLAUDE.md asked that this be confirmed rather than assumed. It was, and the
answer shapes the whole Install screen, so it is written down here.

## The short version

**There is no remote-install command on a jailbroken Vita.** Uploading a VPK
does not install it. The install is performed by VitaShell on the device and
requires the extended-permissions prompt to be confirmed there.

## What was checked

### VitaShell's FTP server

VitaShell exposes an FTP server (SELECT toggles it, port 1337 by default). It
implements file operations only — `LIST`, `RETR`, `STOR`, `DELE`, `MKD`, `RMD`,
`RNFR`/`RNTO`, `SIZE`. There is no vendor extension for installing, and no
watched folder that auto-installs what appears in it. `ux0:/vpk` is a
convention among users, not a hook.

### vitacompanion

`vitacompanion` (devnoname120) is the plugin most often mistaken for an install
channel. It runs an FTP server on 1337 and a **command server on TCP 1338**
that accepts newline-terminated text commands. Its command table is fixed and
defined in `src/cmd_definitions.c`:

| Command | Arguments | Effect |
| --- | --- | --- |
| `help` | — | list commands |
| `version` | — | plugin version |
| `launch` | Title ID | start an app |
| `kill` | Title ID | stop an app |
| `destroy` | — | stop everything running |
| `reboot` | — | reboot the console |
| `screen` | `on` \| `off` | screen power |
| `nosleep` | `on` \| `off` \| `status` | suspend prevention |
| `press` / `release` | button | synthetic input |
| `wait` | duration | delay, for scripting input |

There is **no `installvpk`, no `install`, no `promote`**. Anything that appears
to install remotely is either shipping its own on-device helper or driving the
UI with `press`/`wait` — which is fragile, silently breaks when a menu changes,
and can confirm a prompt the user never saw.

### The install itself

Installing a package calls `scePromoterUtil` on the device. It runs behind the
extended-permissions confirmation, on the console, by design.

## What this app does instead

`vsp::TransferQueue` runs an install-flagged upload as:

1. **Checksum** the local file (SHA-256) before anything is sent.
2. **Upload** it to the staging folder for its kind — `ux0:/vpk` for packages.
3. **Verify**: ask the device for the stored file's size and compare against
   the local size. A mismatch **fails the job**, and the package is never
   offered for install.
4. **Hand off**: the job moves to `AwaitingDevice` and the UI shows the package
   name, its path, and `press X in VitaShell`.

FTP offers no remote hash, so step 3 is the strongest end-to-end check
available over this transport. The app says which check it managed — "Verified
· sha256 …" when both, "Size check unavailable" when the device would not
answer `SIZE` — and never implies more than it did.

Where `vitacompanion` is detected (`version` answers on 1338), two genuine
conveniences are added:

- `nosleep on` for the duration of the queue, so a long upload is not cut short
  by the console suspending. The **AWAKE** chip in the title bar reflects it.
- `launch VITASHELL` behind the **Open VitaShell** button, so the confirmation
  is one tap away rather than a hunt through the home screen.

Neither is required. With no plugin installed the app is fully functional and
simply reports the companion as unavailable.

## Why not automate the last tap

It could be faked with `press`. It is not, because the prompt being confirmed
grants a package extended permissions on the user's console. A tool that
answers that prompt on the user's behalf, from another device, is doing
something the user should be doing themselves. The FAQ behind the lightbulb
says this in the app, in one paragraph, where a curious user will find it.

## Sources

- [vitacompanion — README](https://github.com/devnoname120/vitacompanion/blob/master/README.md)
- [vitacompanion — command definitions](https://github.com/devnoname120/vitacompanion/blob/master/src/cmd_definitions.c)
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell)
- [Vita Hacks Guide — finalizing setup](https://vita.hacks.guide/finalizing-setup-(3.60))
- [ConsoleMods Wiki — VitaShell](https://consolemods.org/wiki/Vita:VitaShell)
