# Architecture

One core, two shells. Nothing that decides anything lives in the UI.

```
                    ┌──────────────────────────────┐
   desktop/main.cpp │                              │ android/ (manifest only)
   + qml/           │        VitaDevice            │ + the same qml/
        ────────────▶  the one object QML binds to ◀────────────
                    └───────────────┬──────────────┘
                                    │ owns
        ┌──────────────┬────────────┼─────────────┬──────────────┐
        ▼              ▼            ▼             ▼              ▼
 RemoteTransport CompanionClient TransferQueue  DropStage   RemoteBrowser
  (abstract)        (1338)         scheduler    Model         Model
   ▲       ▲                          │           │              │
   │       │                          │           ▼              │
FtpClient UsbTransport                │    PackageInspector      │
 (1337)   (mounted vol.)              │    ├ ZipReader           │
        │                             │    └ SfoReader           │
        │                             │           │              │
        └─────────────────────────────┴───────────┴──────────────┘
                                      │
                        PathUtils · VitaPaths · MetadataDb
```

## The pieces

**`RemoteTransport`** — the abstract contract behind every way this app can
reach a Vita's filesystem: `list`, `upload`, `download`, `makeDirectory`,
`removeFile`, `removeDirectory`, `rename`, `requestSize`, `abortAll`, plus the
signals that answer them (`commandFinished`, `listingReady`, `sizeReady`,
`transferProgress`, `connectionLost`/`disconnected`). `TransferQueue` and
`RemoteTreeScanner` are written against this interface only, which is what
lets USB support exist as a second implementation rather than a second copy
of the install/browse/transfer logic. See [usb.md](usb.md).

**`FtpClient`** — asynchronous FTP over two sockets, and the Wi-Fi
implementation of `RemoteTransport`. Every request is queued and answered with
a `commandFinished` carrying its own request id, so a caller never has to
guess which reply is theirs. Uploads are fed to the socket as it drains in
128 KB chunks, so a 4 GB package never sits in memory. Passive-mode replies
are honoured for the *port* only; the host is taken from the control channel's
peer, so a malformed `227` cannot redirect a transfer to a third machine.

**`UsbTransport`** — the USB implementation of `RemoteTransport`. VitaShell's
USB mode is not a protocol, so there is no wire dialect to speak: it hands the
Vita's raw memory-card partition to the host as a standard USB Mass Storage
device, which the OS mounts like any flash drive. "Connecting" is pointing at
that mounted folder, and every request runs as plain `QDir`/`QFile` work on a
worker thread, kept behind the same async request-id contract `FtpClient`
established (`QtConcurrent` + `QPromise` for progress) so `TransferQueue`
cannot tell the difference. Full research and the reasoning behind every
design choice here: [usb.md](usb.md).

**`CompanionClient`** — optional. Probes TCP 1338 for `vitacompanion` and, if
it answers, offers `nosleep` and `launch`. Commands are rejected outright if
they contain a newline or a control character rather than being escaped; none
of the verbs need those.

**`TransferQueue`** — the job list *and* the scheduler, and a
`QAbstractListModel` so QML binds straight to it. One job runs at a time
because the Vita's server is single-connection. Install-flagged uploads run
checksum → upload → verify → hand off; see
[install-trigger.md](install-trigger.md).

Jobs come in two shapes. A *file job* moves one file. A *tree job* moves a
folder — a theme, an already-unpacked game, a savedata folder coming back — as
a sequence of directory creations followed by one transfer per file, reported
to the UI as a single unit with a file counter. A theme arrives as an archive
and is unpacked to scratch space first; an unpacked game is walked where it
lies, since copying a folder that can run to gigabytes to send it would be
absurd. Both shapes share one progress model, so the
queue total stays meaningful when the two are mixed.

**`PackageInspector`** — opens a VPK (a ZIP) and reads `sce_sys/param.sfo` for
the authoritative Title ID, name, version and category, plus `icon0.png` when
present. Retail Title IDs are distinguished from homebrew ones so the app never
claims a catalogue match it does not have. `ZipReader` supports store, deflate
and ZIP64, reads members in memory only (so there is no extraction path to
traverse), and caps every read.

**`MetadataDb`** — the offline index plus any cover-pack folder the user points
at. `param.sfo` always wins for the name; the database only fills gaps and adds
art.

**`ThemeReader` / `ArchiveExtractor`** — theme identification and the only code
that writes archive contents to disk. See [themes.md](themes.md).

**`RemoteTreeScanner`** — walks a remote folder breadth-first over the existing
connection. Folder download needs the whole file list before it can report a
total, and the Vita answers one command at a time, so the walk is a queue of
LIST requests rather than recursion.

**`Thumbnailer`** (in `media/`) — previews for staged files: an embedded icon,
a scaled photo, a decoded video frame, or a plate drawn from a theme's own
declared colours. It lives outside the core because it needs Qt Gui and a
decoder, and the models reach it through `ThumbnailProvider` so the core keeps
neither. Qt Multimedia is optional at build time; without it videos get the
drawn plate instead of a frame.

**`PathUtils` / `VitaPaths`** — all input hygiene and every `ux0:/…` string in
the code base, in two files.

## Design system

`desktop/qml/Theme.qml` is a singleton holding the entire visual language:
planes, ink, accents, one control height, one radius, two type families.

- **Palette**: white, black, green, blue, purple, plus structural greys. Blue
  carries identity and navigation, green means "this went through", purple
  flags anything needing a second look — including errors, since red is not in
  the palette.
- **Contrast**: the base is `#06090F` and the primary ink is `#E9EFF8`, not
  pure white. Accents do real work on labels, active states and icons rather
  than decorating a black-and-white app.
- **Uniformity**: `Theme.controlHeight` is used by every field, button and
  chip, so boxes line up by construction instead of by eye.
- **Icons** are stroked vector paths on a 24×24 grid, drawn by `Icon.qml` with
  Qt Quick Shapes. No image-format plugin is needed on any platform and the
  colour is a binding. One caveat found the hard way: a Shape is **not** bounded
  by an ancestor's scissor clip in this Qt build, so a delegate outside a
  list's viewport still paints its icons over whatever is below. Every
  scrolling list therefore goes through `ClippedList.qml`, which renders into a
  layer -- the one thing that does bound them -- over an opaque backdrop,
  because a layer's untouched pixels composite as black rather than as nothing.
- **State is never carried by hue alone.** `Theme.toneIcon()` gives each tone
  its own glyph and `StatusRail.qml` draws an alert as a notched bar rather
  than a solid one; failures additionally carry the word FAILED, an outlined
  card, and a triangle badge on the rail instead of a counter. Purple still
  means "look at this", but nothing depends on the reader seeing purple.
- **Text is minimal.** The only prose in the product is behind the lightbulb in
  the bottom-left corner, as question and answer. Protocol detail lives in the
  collapsible drawer, never in the main view.

## Selection

Selection state lives in `RemoteBrowserModel`, not in the view, because
rubber-band sweep, ctrl-click, shift-range and the touch lasso all have to
agree on one set — and every batch action reads that same set. In `BrowseView`
a single overlay owns all pointer gestures and computes row indices itself, so
the delegates stay pure presentation.

On touch a finger drag has to scroll, so the sweep is mouse-only; a long press
turns on tap-to-toggle mode with checkboxes instead. Same model, same actions.

## Testing

`core/tests/MockVitaServer` is an in-process FTP server backed by a real
directory that speaks the same narrow dialect the Vita does — passive mode
only, unix long listings, minimal auth. It can also be told to report wrong
sizes, which is how the verification failure path is tested. It has caught six real
bugs so far:

- a race where a small upload closed its data socket before the server's `150`
  was read;
- the client's intolerance of a preliminary reply arriving after the data
  channel had already closed;
- the client writing upload data **before** the server acknowledged `STOR`,
  which RFC 959 does not allow and which made small files fail intermittently
  against a server that had not yet read the command;
- a runaway refresh, where a completed listing scheduled another listing and
  the connection filled with `LIST` forever;
- an abort that left the server still owing replies, so cancelling one job made
  the next one read answers meant for the job before it;
- navigating with `LIST <path>` instead of `CWD` + `LIST`, which made every
  folder show the same mount-point list on real hardware.

The last two only showed up when the whole app was driven against the runnable
`mockvita`, which is why that binary exists. The navigation bug shipped anyway,
because the mock was *correct* where the real server is *quirky*: it resolved
the path argument that VitaShell quietly ignores. A mock that only implements
the specification will certify a client that cannot talk to the device — see
[browsing.md](browsing.md).

`core/tests/tst_usb` covers `UsbTransport` the same way `tst_transport`
covers `FtpClient`, driven against a plain directory carrying the same
signature (`id.dat` + a VitaShell folder) and layout a mounted card has —
there being no protocol to fake for USB, unlike FTP. `tst_device` carries USB
versions of the core `VitaDevice` integration tests alongside the Wi-Fi ones,
proving the two modes share one code path rather than forking. See
[usb.md](usb.md) for what could and could not be verified without real
hardware.

Two more additions in the same spirit: `sidebarRootListsEveryPartitionNotJustTheCurrentMount`
and `sidebarPackagesListsWhatIsActuallyThere` drive `Device.quickLocations()`'s
own computed paths (not hand-typed literals) through `navigateTo()`, so a
future regression in how a sidebar entry is *wired* — as opposed to the
`CWD`/`LIST` protocol this section already covers — gets caught the same way.
`noNpDrmDumpSplitsAcrossFixedDestinations` (see [nonpdrm.md](nonpdrm.md))
covers the one genuinely new upload shape added since: one dropped folder
becoming several jobs, each landing at its own fixed destination.
