#pragma once

#include <QString>
#include <QStringList>

// Path and input hygiene. Everything that turns user input or a remote
// directory listing into a path we actually act on goes through here, so
// traversal ("../") and separator injection are rejected in exactly one place.
namespace vsp::path {

/// Strict dotted-quad check. Rejects leading zeros, out-of-range octets and
/// anything with stray characters -- an IP field is not the place to be lenient.
bool isValidIPv4(const QString &address);

/// True for a port in the usable range.
bool isValidPort(int port);

/// Reduces an arbitrary string to something safe to use as a single path
/// component: separators, control characters, NUL, device-reserved names and
/// trailing dots/spaces are all removed. Never returns an empty string.
QString sanitizeFileName(const QString &name);

/// True when \a name is a plain component with no separators and no "." / ".."
/// semantics. Used to validate anything a remote server hands us.
bool isSafeComponent(const QString &name);

/// Collapses duplicate slashes and "." segments and resolves ".." without ever
/// escaping the root. Always returns a path starting with '/', never trailing
/// '/' (except for the root itself). Vita mount prefixes ("ux0:") survive.
QString normalizeRemote(const QString &remotePath);

/// Appends a single component to a remote directory, sanitising the component.
QString joinRemote(const QString &base, const QString &leaf);

/// Directory containing \a remotePath, or "/" at the top.
QString parentOfRemote(const QString &remotePath);

/// Last component of \a remotePath.
QString baseNameOfRemote(const QString &remotePath);

/// True when \a remotePath is a Vita mount root such as "/ux0:" or "/ur0:".
bool isMountRoot(const QString &remotePath);

/// The mount prefix ("ux0:") of a remote path, or an empty string.
QString mountOf(const QString &remotePath);

/// Joins a local directory with a remote-supplied file name, guaranteeing the
/// result stays inside \a localDir. Returns an empty string if it cannot.
QString safeLocalTarget(const QString &localDir, const QString &untrustedName);

/// "1.4 GB" style formatting for the UI.
QString humanSize(qint64 bytes);

/// "1.2 MB/s" style formatting for the UI.
QString humanRate(double bytesPerSecond);

/// \a absolutePath with a leading home-directory prefix replaced by "~", for
/// display only -- a local folder field otherwise shows the OS account name
/// verbatim to anyone looking at (or screenshotting) the app on someone
/// else's machine. Paths outside the home directory are returned unchanged.
/// The real, uncollapsed path is what every actual file operation still
/// uses; this never touches storage, only what is drawn on screen.
QString collapseUserHome(const QString &absolutePath);

/// The inverse: a leading "~" (as this app's own fields, or any shell, would
/// write one) is expanded back to the real home directory before the path is
/// used for anything. Anything not starting with "~" is returned unchanged,
/// so typing or pasting a normal absolute path still works exactly as before.
QString expandUserHome(const QString &pathOrDisplay);

} // namespace vsp::path
