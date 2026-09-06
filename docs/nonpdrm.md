# NoNpDrm-style dumps

A third folder-format install shape, alongside the two `docs/themes.md`
covers. Where an unpacked-game drop is *one* folder that lands at
`ux0:app/<serial>/`, a NoNpDrm-style dump is a *parent* folder holding up to
three fixed subfolders, each of which has to land somewhere different:

```
My Game Dump/
  app/
    PCSB00550/            <- exactly what a standalone FolderGame drop is
      eboot.bin
      sce_sys/param.sfo
      ...
  addcont/
    PCSB00550/            <- DLC, named by the game's own serial
      ...
  license/                <- optional
    PCSB00550/
      ...
```

- `app/*` → `ux0:app/*`
- `addcont/*` → `ux0:addcont/*`
- `license/*` → `ux0:license/*` (if present)

Any one of the three may be absent; at least one must be present, or the
folder is not this format at all.

## Detection

`PackageInspector::looksLikeNoNpDrmDump()` recognises the shape by exactly
one thing: the dropped folder directly contains one or more of `app`,
`addcont`, `license` as top-level subfolders. A standalone unpacked-game
folder never has this shape — it carries `sce_sys/` (or is itself named by a
Title ID) directly at its own root, not inside a folder named `app`. The two
are checked in that order in `DropStageModel::addPaths()`: NoNpDrm shape
first, ordinary folder inspection only if that shape is absent, so a game
folder is never misread as a dump and a dump is never misread as one huge
game folder.

## Why this needed no new upload logic

Each serial-numbered folder inside `app`/`addcont`/`license` is, structurally,
*exactly* what a standalone `FolderGame` drop already is: a directory that
has to land under its own name at a fixed destination, contents preserved
exactly. So `PackageInspector::inspectNoNpDrmDump()` does nothing but walk
each present subfolder's immediate children and call the existing
`inspectFolder()` on each one — the same function, and the same title-ID
extraction (`param.sfo` first, the folder's own name as a fallback), that a
lone game-folder drop already uses. Every result keeps `PackageKind::FolderGame`,
which is what makes `TransferQueue::enqueueFolderInstall()`,
`isDirectoryPayload()`, and the "Refresh LiveArea in VitaShell" confirmation
wording all apply completely unmodified — a NoNpDrm part is queued and
installed by calling that one existing function once per detected part, never
a second copy of it.

The only new thing each part carries is `PackageInfo::noNpDrmRoot` — which of
`app`/`addcont`/`license` it came from — because that is not something
`PackageKind` can express (one dropped folder can hold parts for more than
one destination at once). `DropStageModel` reads it to route the destination
through `vitapaths::fixedRootFor()` instead of the usual kind-based lookup,
and to show a clearer label ("App Data" / "Add-on Content" / "License")
than the generic "Game folder" `PackageKind::FolderGame` would otherwise
print.

## The same shape, one level up

A real bug reached hardware here: dragging `app` or `addcont` **on its own**
(not the parent dump wrapping them) was never handled as this format at all,
because `looksLikeNoNpDrmDump()` only ever looked *inside* the dropped
folder for `app`/`addcont`/`license` — it never asked whether the dropped
folder *was* one of them. The dropped `app` folder fell through to ordinary
folder inspection, which computes its destination from `PackageKind` alone
(`FolderGame` → always `ux0:app`) with no way to say "route me to
`ux0:addcont` instead" — so a standalone `app` and a standalone `addcont`
both landed on `/ux0:/app`, silently, regardless of which one was actually
dropped. Dragged separately as two drops, add-on content could overwrite the
game's own installed files at the same path.

`PackageInspector::looksLikeStandaloneContentFolder()` /
`inspectStandaloneContentFolder()` close this the same way the dump case
works: they just enter the shared child-walking helper
(`inspectContentRoot()`, factored out of `inspectNoNpDrmDump()`) directly at
the dropped folder instead of at one of its children, tagging every result
with the dropped folder's own name as `noNpDrmRoot`. Checked in
`DropStageModel::addPaths()` right after the parent-dump case: dump shape
first, standalone-name shape second, ordinary folder inspection last, so
none of the three shapes can be misread as another.

## What the UI shows before Start

Same pattern as any other folder-format install: title, a kind chip, and a
destination line with a chevron. Two parts from the same drop appear as two
separate staged rows — e.g. one titled "PCSB00550" going to `/ux0:/app`
tagged **App Data**, another titled "PCSB00550" going to `/ux0:/addcont`
tagged **Add-on Content** — so what will be uploaded and where is visible
per part, not folded into one ambiguous entry.

## Test coverage

`core/tests/tst_device.cpp`:

- `noNpDrmDumpIsRecognisedAndAGameFolderIsNot` — the detection boundary in
  both directions.
- `noNpDrmDumpSplitsAcrossFixedDestinations` — a dump with `app/PCSB00550`
  (a full game folder) and `addcont/PCSB00550` (no `param.sfo`, named by
  serial) staged, started, and verified to land at `ux0:app/PCSB00550` and
  `ux0:addcont/PCSB00550` respectively, nested structure intact, with the
  same "Refresh LiveArea in VitaShell" wording on both.
- `standaloneAppAndAddcontFoldersRouteToDistinctDestinations` — the
  hardware bug: `app` and `addcont` dropped as two separate drops (not one
  parent) land at two *different* destinations, and the add-on content is
  confirmed absent from the game's own installed folder afterward, not just
  present in its own.
- `standaloneLicenseFolderRoutesToLicenseDestination` — the third shape,
  dropped alone.
- `folderInstallMergesRatherThanReplacingExistingDestinationContent` — a
  file already present at the destination before a folder-format install
  runs is still there afterward; nothing about a folder install clears the
  destination first, only `MKD` (already tolerant of "already exists") and
  per-file `STOR`s that only ever touch the paths the drop itself provides.
