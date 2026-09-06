# Why browsing broke on real hardware

Every folder in the browser showed the same ten entries — `os0:`, `pd0:`,
`sa0:`, `tm0:`, `ud0:`, `uma0:`, `ur0:`, `ux0:`, `vd0:`, `vs0:` — while the
breadcrumb happily reported wherever the user had clicked. Uploads and
downloads worked fine throughout, which is what makes this worth writing down.

## The cause

The client asked for a listing with the path as an argument:

```
TYPE I
PASV
LIST /ux0:/video
```

That is legal FTP, and it is not what VitaShell's server does with it.
VitaShell uses [libftpvita](https://github.com/xerpi/libftpvita), whose
`cmd_LIST_func` reads:

```c
int n = sscanf(client->recv_cmd_args, "%[^\r\n\t]", list_path);

if (n > 0 && file_exists(get_vita_path(list_path)))
    list_cur_path = 0;

if (list_cur_path)
    send_LIST(client, client->cur_path);   /* <- silent fallback */
else
    send_LIST(client, list_path);
```

The path argument is honoured **only if it stats**. Otherwise the request
quietly degrades to the working directory — no error, no warning, a perfectly
valid `150`/`226` exchange around the wrong data.

And the working directory starts at `/`, which `send_LIST` treats as a special
case that emits the list of mount points. So a failed resolution produces
exactly the ten entries above, forever, because the client never moved the
server anywhere else.

### Why the path failed to stat

`get_vita_path` just drops the leading slash, so `/ux0:` becomes `ux0:`, and
`file_exists` calls `sceIoGetstat("ux0:")`. A bare device root does not stat on
a Vita; it needs the trailing slash, `ux0:/`.

`cmd_CWD_func` knows this and fixes it up:

```c
/* If the path is like: /foo: add an slash */
if (strrchr(tmp_path, '/') == tmp_path)
    strcat(tmp_path, "/");
```

`cmd_LIST_func` has no such fixup. So `CWD /ux0:` succeeds and `LIST /ux0:`
silently does not — and `/ux0:` is the browser's entry point, which is why
nothing below it was ever reachable.

Transfers were unaffected because `RETR`, `STOR`, `MKD`, `DELE` and `RMD` all
go through `gen_ftp_fullpath`, which handles an absolute path properly and has
no fallback to fall into.

## The fix

Navigate, then list where you are:

```
CWD /ux0:/video
TYPE I
PASV
LIST
```

`LIST` with no argument is the one form every server agrees on, and it makes
the server's working directory the single source of truth instead of a hint
the server is free to ignore. A folder that cannot be opened now fails at the
`CWD`, with the server's own message, instead of returning someone else's
contents.

## Why the tests did not catch it

The in-process stand-in server resolved the `LIST` argument faithfully. It was
a *correct* FTP server, and the device is a *quirky* one, so the mock certified
a client the hardware could not use.

It now reproduces the quirk deliberately: `LIST` uses its argument only when
that argument stats, a bare device root does not stat, and `/` answers with the
mount-point list. Running the old client against it reproduces the hardware
symptom exactly, down to the ten device names.

The lesson is narrow and worth keeping: **a mock should imitate the device, not
the specification.** Where the two differ, the device wins, and the difference
is precisely where the bugs live.

## What the regression tests assert

Not that the breadcrumb changed — that was never in doubt, and it was the thing
that made the bug look like it was working:

- `navigatingIntoAFolderChangesWhatIsListed` — the listed **names** differ
  before and after navigating, and the new listing holds the file it should.
- `everyFolderListsItsOwnContents` — four folders, four pairwise-different
  listings, none of them the mount-point list.
- `navigatingToADeviceRootListsThatDevice` — `/ux0:` lists the card, the case
  that broke.
- `theRootReallyDoesListTheDevices` — `/` still lists mount points, since that
  is the one place the old output was the right answer.
- `goingBackUpListsTheParentAgain` — navigating up re-lists the parent.
- `goingUpFromAMountRootReachesTheTrueRoot` — "Up" from `/ux0:` (or any mount
  root) reaches `/` and lists every partition, the same target and the same
  `navigateTo("/")` call the sidebar's "Root" entry uses. The two used to
  disagree — Up simply stopped at the mount root — which was a leftover from
  before Root itself pointed at the true root; there was no technical reason
  for the two to differ, so `VitaDevice::navigateUp()` now takes this one
  extra step instead of stopping short.
- `listingNavigatesWithCwdAndListsTheWorkingDirectory` — pins the protocol:
  a `CWD` must precede the `LIST`, and no `LIST` may carry a path.
- `listingARefusedFolderFailsInsteadOfShowingSomethingElse` — a missing folder
  reports an error rather than a listing of somewhere else.
