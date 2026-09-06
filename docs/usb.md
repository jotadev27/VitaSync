# How VitaShell's USB mode actually works

CLAUDE.md asked that this be confirmed rather than assumed, the same way
[install-trigger.md](install-trigger.md) and [themes.md](themes.md) were. It
was, against VitaShell's own source, and the answer shapes the whole USB
implementation, so it is written down here.

## The short version

**USB mode is not a protocol.** VitaShell hands the Vita's raw storage
partition to the host as a standard **USB Mass Storage** device — the same
device class any USB flash drive uses. The host's own built-in driver mounts
it as an ordinary removable volume with a real FAT/exFAT filesystem on it.
There is no FTP, no MTP, no vendor protocol, and — because the Vita's own
network stack and `vitacompanion`'s command server keep running independently
of this — **also no reachable FTP or companion connection while USB mode is
active**, since VitaShell blocks in a modal "USB Connected" dialog on the
console for the duration.

## What was checked

Source: [TheOfficialFloW/VitaShell](https://github.com/TheOfficialFloW/VitaShell),
`usb.c`, `usb.h`, and the `initUsb()` / `dialogSteps()` machinery in `main.c`.

```c
// main.c: initUsb() picks the physical device by user setting, then:
usbdevice_modid = startUsb("ux0:VitaShell/module/usbdevice.skprx",
                            path /* e.g. "sdstor0:xmc-lp-ign-userext" */,
                            SCE_USBSTOR_VSTOR_TYPE_FAT);

// usb.c: startUsb() —
res = sceMtpIfStopDriver(1);                       // MTP is not used
res = sceUsbstorVStorSetDeviceInfo("\"PS Vita\" MC", "1.00");
res = sceUsbstorVStorSetImgFilePath(imgFilePath);   // the raw partition
res = sceUsbstorVStorStart(type);                   // type = FAT
```

Three things fall out of this directly:

1. **`SCE_USBSTOR_VSTOR_TYPE_FAT` + `sceUsbstorVStorStart`** is Sony's virtual
   mass-storage API — genuine USB Mass Storage Class, not MTP. The
   `sceMtpIfStopDriver(1)` call even explicitly turns MTP *off* first, so the
   two are mutually exclusive on this firmware.
2. **The path handed to `SetImgFilePath` is a raw low-level device path**
   (`sdstor0:xmc-lp-ign-userext` for internal memory, or the SD2Vita/PSVSD/
   game-card equivalents), not a folder. The *entire physical partition* is
   exported, not a slice of it — this is the same partition `ux0:` is backed
   by in VitaShell's own path-redirection scheme, so what the host sees at
   the mounted volume's root is the same folder set VitaShell's FTP server
   shows for `ux0:` (`app`, `appmeta`, `license`, `vpk`, `video`, `photo`,
   `user`, `VitaShell`, …) — **with no further `ux0:` segment**, because that
   name was only ever a VitaShell-side alias for this same partition.
3. **`vitashell_config.usbdevice`** selects *which* physical partition gets
   exported: Memory Card (internal storage, the default), Game Card,
   SD2Vita, or PSVSD. Only one at a time — USB mode exports one raw device,
   not a merged view.

### What the user does on the Vita

Per VitaShell's own README and the `DIALOG_STEP_FTP` / `DIALOG_STEP_USB`
states in `main.c`: from VitaShell's Start menu, the same panel that offers
**FTP** also offers **USB**. Picking USB (with the cable already attached)
calls `initUsb()`; picking FTP starts `ftpvita`. They are alternatives in the
same menu, not simultaneous options — confirmed by `stopUsb()` restarting the
MTP driver and remounting the internal partitions on exit, which only makes
sense if nothing else was using them at the same time.

## Scope decision: Memory Card mode only

VitaSync's USB support targets the **default "Memory Card" USB mode**
only — the one whose exported partition's root matches `ux0:`'s FTP-visible
layout, which is what every other feature in this app (the VPK staging
folder, the video/photo/theme routing in `VitaPaths`, the savedata paths) was
already written against. Game Card, SD2Vita and PSVSD USB modes export a
*different* physical partition with a different (or absent) folder layout and
are out of scope, the same way `.pkg` support is out of scope — not
attempted, not silently mishandled. `UsbTransport` maps every path it is
asked for through a single `ux0:` prefix; anything else is refused with a
clear message rather than guessed at.

## What this means for the app

Because USB mode is a real, host-mounted filesystem and not a protocol, the
question CLAUDE.md asked — "can the existing core logic run unmodified
against a mounted path, or does it need a separate transport?" — has a
two-part answer:

- **Identification, routing, VPK/folder install and theme install all run
  completely unmodified.** None of `PackageInspector`, `ThemeReader`,
  `VitaPaths`, `MetadataDb` or `DropStageModel` know or care how bytes reach
  the device; they only ever produced package metadata and destination
  paths, which are transport-agnostic already.
- **The transfer layer needed a second implementation**, because `FtpClient`
  is inescapably built around FTP's own reply/data-socket state machine.
  What it did *not* need was a second copy of `TransferQueue` (1200 lines of
  hardware-tested queueing, verification, cancel and reconnect logic) or a
  second `RemoteTreeScanner`. Both were already talking to `FtpClient`
  through a narrow surface — `list`, `upload`, `download`, `makeDirectory`,
  `removeFile`, `removeDirectory`, `rename`, `requestSize`, `abortAll`, plus
  a handful of signals — so that surface was pulled out into an abstract
  `RemoteTransport` interface (`core/vsp/RemoteTransport.h`). `FtpClient` now
  implements it with its behaviour byte-for-byte unchanged (confirmed by the
  full existing suite passing before and after with no test edits); a new
  `UsbTransport` implements the same interface against a mounted folder with
  plain `QDir`/`QFile` calls.

`VitaDevice` holds both transports and a `RemoteTransport *m_active` pointing
at whichever is live (defaulting to the FTP client, so every call site that
used to dereference it unconditionally still can). `TransferQueue` is
re-pointed at the active transport only when it actually changes — a Wi-Fi
session that drops and reconnects to itself never touches this, so that path
gained no new moving parts.

### Keeping the async contract without a socket

`TransferQueue` was written against FTP's request/reply shape: every call
returns a request id immediately and is answered later by a signal, one
request in flight at a time. `UsbTransport` preserves that contract exactly,
even though the underlying work is just local file I/O:

- Each request is handed to a worker thread via `QtConcurrent::run`, keeping
  the GUI thread free exactly as the socket-based client does.
- Upload/download report progress through Qt 6's `QPromise`
  (`setProgressRange`/`setProgressValue`), translated back into the same
  `transferProgress(requestId, done, total)` signal `FtpClient` emits, so
  `TransferQueue`'s progress bars and rate calculation need no branch for
  which transport is running.
- Only one worker runs at a time, mirroring the FTP side's single-connection
  discipline — not because a filesystem can't do more, but because
  `TransferQueue`'s bookkeeping (one running row, one verify request) assumes
  it, and diverging here would be exactly the kind of forked-per-transport
  behaviour this design was meant to avoid.
- Cancel is synchronous from the caller's point of view (`abortAll()` answers
  the in-flight request with "Cancelled" immediately) and cooperative
  underneath (the copy loop checks `promise.isCanceled()`), matching
  `FtpClient::abortAll()`'s own immediate-answer behaviour.

### Detecting a Vita among the OS's mounted drives

There is no USB descriptor string worth trusting here — `sceUsbstorVStorSetDeviceInfo("\"PS Vita\" MC", "1.00")`
sets a SCSI vendor/product string, but nothing in this app's environment has
easy portable access to raw SCSI descriptors, and relying on it would fail
silently on any host that normalises or hides it. Instead, `UsbTransport::looksLikeVitaVolume()`
looks at the mounted filesystem's *contents*: every Vita memory card carries
an `id.dat` file written by the Vita's own OS, and pairing that with a
VitaShell install folder (`VitaShell/` or `app/VITASHELL/`, present on
essentially every jailbroken card, since VitaShell is what exposes USB mode
in the first place) is specific enough that an unrelated USB drive is very
unlikely to match both. `refreshUsbCandidates()` scans `QStorageInfo::mountedVolumes()`
for this signature; a folder that does not match can still be pointed at
manually (the same "advanced override" pattern the app already uses for
video-folder routing), and is clearly labelled unverified rather than
refused — the same philosophy `MetadataDb` uses for an unmatched title: a
wrong guess should be visible, not fatal.

### Mid-transfer disconnect

There is no control channel to watch for a dropped socket, so `UsbTransport`
polls `QStorageInfo(rootPath)` every three seconds while attached, plus
checks it whenever a request fails. If the volume is genuinely gone
(`!isValid() || !isReady()` — true once the OS has actually unmounted it, not
merely because a subfolder disappeared) it raises `connectionLost()` then
fails the in-flight request then `disconnected()`, in that order — identical
to `FtpClient::handleConnectionLoss()`'s ordering, so `TransferQueue`'s
existing "put the job back in the queue, the connection is already being
retried" behaviour (`jobPostponed`) applies unchanged. `VitaDevice::scheduleReconnect()`
(the same backoff, five attempts, 2s→30s timer the Wi-Fi path uses) retries
re-attaching to the same volume path on its next tick, falling back to a
fresh auto-detect if that fails. This distinction — a whole unmounted volume
versus a merely-missing file — is exercised in `core/tests/tst_usb.cpp`
(`aMissingSubfolderFailsWithoutClaimingTheVolumeIsGone`); the reverse case,
an OS actually tearing down a mounted volume mid-request, needs a real
mount/unmount this sandboxed environment cannot perform, so it is
watched-but-not-unit-tested, the same status CLAUDE.md already records for
the one FTP disconnect report that could not be reproduced against
`MockVitaServer`.

The `QStorageInfo` check itself runs on a worker thread (`checkMountAsync()`),
never the GUI thread — see "First hardware pass" below for why that matters
in practice, not just in theory.

## First hardware pass: three real bugs

The first real-console USB test found three problems, all fixed in the same
pass. The third explains why the first two were worth taking seriously
instead of assuming they were test artifacts: this app's status bar was
telling the truth about something being wrong.

### 1. The top bar showed a host:port address over USB

**Symptom:** the status bar read something shaped like `127.0.0.1:2202`
while connected over USB — an address, on a link that is a mounted folder
and was never a socket.

**Root cause:** `TopBar.qml` bound its address text directly to
`Device.host`/`Device.port`, unconditionally, whenever `Device.connected`
was true — a leftover from when Wi-Fi/FTP was the only mode there was.
Those two fields are only ever written by the FTP side (`setHost`/`setPort`,
loaded once from `QSettings` at startup); nothing in the USB connect path
touches them, so they simply kept showing whatever they last held. **The
underlying transport was not affected** — `VitaDevice::m_active` correctly
pointed at `UsbTransport` throughout, confirmed by reading every call site
that dispatches through it — this was a display bug, not a routing one.

**Fix:** `VitaDevice::connectionSummary()` / `connectionDetail()` are
mode-aware (the mounted volume's own folder name and the literal word "USB"
over USB; host and `:port` over FTP), and `TopBar.qml` now binds to those
instead of the raw fields. Covered by
`usbModeNeverShowsAHostAndPortLikeAFtpSession` in `tst_device.cpp`.

Worth being explicit about, since this looked at first like it might explain
the other two: it doesn't, directly. But it was still worth fixing as its
own bug, and checking it thoroughly is what turned up bug 2 below.

### 2. Navigating Root (or generally) over USB felt slow, hung, and never left ux0:

**Symptom:** clicking Root, or navigating between folders generally, felt
slow to the point of seeming disconnected, and Root specifically never
showed anything but `ux0:`'s own contents.

This turned out to be two separate bugs stacked on top of each other.

**2a — Root can never succeed, and used to fail silently.** Root's target is
the true filesystem root (`/`, fixed for the FTP side earlier — see
`docs/browsing.md`), which lists every partition the device has. Over USB
that request is fundamentally out of scope: only one physical partition is
ever exposed (see "Scope decision" above), so `UsbTransport::mapToLocal()`
correctly refuses anything outside `ux0:`, Root included, with a real error
message. But `VitaDevice::navigateTo()` (and every other caller) does
`requestId = m_active->list(...)` and only *afterwards* records `requestId`
to match the reply against — which is safe with `FtpClient`, whose requests
are always a genuine socket round trip and can never finish before the
caller's own assignment does. `UsbTransport`'s reject-before-any-I/O path
used to answer inline, in the same call, which meant the reply could arrive
for a request id nobody had recorded yet. The reply was not lost so much as
unmatchable — dropped by the very check meant to route it — so the status
bar never updated and Root looked exactly like nothing had happened.
**Fixed** by answering on the next event-loop turn instead
(`UsbTransport::rejectRequest()`), the one guarantee every caller already
depended on and that only this fast-reject path ever violated.

**2b — every failure, including 2a's, used to check the mount status
synchronously on the GUI thread.** `finishRequest()` treated any failed
request as possibly meaning the device had gone away, and checked with
`QStorageInfo::refresh()` right there, before answering. On this machine's
fast local test fixtures that call is instant; against a real USB Mass
Storage device it is a real, physically-slow stat, and running it inline
froze the whole GUI for however long it took — repeatable on every Root
click, and on the periodic three-second health poll besides. **Fixed** by
moving every `QStorageInfo` check onto a worker thread
(`UsbTransport::checkMountAsync()` / `onMountCheckFinished()`), and by
skipping the check entirely for a request like 2a's that never touched the
volume in the first place (`rejectRequest()` never calls it at all) — the
fix for 2a already avoids the single worst offender, and 2b closes the rest.

Covered by `rejectsAMountOtherThanUx0` (asserts no `connectionLost`/
`disconnected` fires and the round trip stays under 200 ms) in
`tst_usb.cpp`, and by `usbRootFailsClearlyInsteadOfHangingOrFalselyDisconnecting`
in `tst_device.cpp`, which asserts the status bar actually reaches an error
state with a readable reason and the connection stays up — the exact
end-to-end path that used to go silent.

### 3. A folder install reported success over USB but nothing landed on the console

**Symptom, and the most serious of the three:** dragging a game's `app`
folder, pressing Start, seeing it reported as sent — and after Refresh
LiveArea on the console, nothing was actually there.

**Root cause:** `QFile::flush()` (used after every write, before reporting a
job complete) only pushes data out of `QFile`'s own buffer and into the
host OS — it says nothing about whether the OS has written it through to
the physical device, and neither does `close()` on any Qt backend. For a
local disk this distinction rarely matters in practice. For a USB Mass
Storage volume it is exactly the failure mode this bug report described:
the host's page cache can hold a "successful" write indefinitely, and if the
cable comes out (or VitaShell remounts the card for its own reasons) before
that cache is flushed, the write never reaches the card at all — the console
sees exactly nothing, no matter how correctly everything upstream of the
write behaved.

**Fix:** `syncFile()` (new, in `UsbTransport.cpp`) calls `flush()` and then
`fsync()` on the raw file descriptor (`FlushFileBuffers` on the native
handle on Windows) before a copy is allowed to report success, in
`runCopyFile()`. A newly created file or directory is also a change to its
*parent* directory's own entries — separate metadata that `fsync`-ing the
file itself does not cover, and which exFAT/FAT drivers (what a Vita card
almost always is) commonly do not journal the way a modern desktop
filesystem does — so `syncDirectory()` additionally syncs the containing
folder, in both `runCopyFile()` and `runMkdir()`. Any sync failure is
reported as a job failure, not silently ignored: the whole point is that the
app must not say "done" until the bytes actually are.

This trades some upload speed for the guarantee that "done" is true — an
`fsync` per file (and per newly created directory) is not free, especially
against a real, slower USB device, but the alternative is exactly the bug
that was reported.

**What this fix cannot claim on its own:** whether the write physically
reached the card is, by definition, not something a test running against a
local filesystem can observe — the local disk backing this project's test
suite is not the failure mode being fixed. The existing upload/download/
mkdir tests (`tst_usb.cpp`, and the folder-install tests in `tst_device.cpp`)
confirm the added `fsync`/`FlushFileBuffers` calls do not break normal
operation and that data still round-trips correctly; only a real console,
with the cable pulled immediately after a reported success, can confirm the
data survives. Flagging this the same way CLAUDE.md already flags the one
FTP disconnect report that could not be reproduced: implemented and
reasoned through, not yet field-proven on this specific claim.

## What was and wasn't verified

The first three items above came from an actual console over an actual USB
cable, and are now fixed and covered by tests exercising the exact code
paths involved (not, in most cases, the physical symptom itself — see each
item's own caveat). Everything about *which partition gets exposed and how*
(the "What was checked" section above) was confirmed by reading VitaShell's
source, not by hardware, and remains unverified in that narrower sense.
Treat the transport as hardware-tested for connect, browse, and one round of
install/navigate bugs; not yet exercised for sustained/large transfers,
concurrent multi-file installs at real USB 2.0 speeds, or an actual
mid-transfer unplug.

## Sources

- [VitaShell — `usb.c`](https://github.com/TheOfficialFloW/VitaShell/blob/master/usb.c)
- [VitaShell — `usb.h`](https://github.com/TheOfficialFloW/VitaShell/blob/master/usb.h)
- [VitaShell — `main.c`](https://github.com/TheOfficialFloW/VitaShell/blob/master/main.c) (`initUsb()`, `dialogSteps()`)
- [VitaShell — README](https://github.com/TheOfficialFloW/VitaShell/blob/master/README.md)
- [USB mass storage device class — Wikipedia](https://en.wikipedia.org/wiki/USB_mass_storage_device_class)
