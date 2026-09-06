#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace vsp {

/// One entry from a remote directory listing.
struct RemoteEntry
{
    QString name;
    QString path;
    bool isDirectory = false;
    qint64 size = -1;
    QDateTime modified;
    QString permissions;
};

/// The contract every way this app can reach a Vita's filesystem has to meet
/// -- FTP over Wi-Fi (`FtpClient`) or a mounted USB volume (`UsbTransport`).
/// `TransferQueue`, `RemoteTreeScanner` and most of `VitaDevice` are written
/// against this interface only, so the install/browse/transfer logic exists
/// exactly once and does not fork per transport. See docs/usb.md for why USB
/// needed a second implementation instead of reusing FtpClient's wire code.
///
/// Every request returns a request id (> 0), answered later by a matching
/// signal, or 0 when the request was rejected outright (bad input, not
/// connected) -- this is `FtpClient`'s original contract, generalised.
class RemoteTransport : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,
        Authenticating,
        Ready,
        Busy
    };
    Q_ENUM(State)

    explicit RemoteTransport(QObject *parent = nullptr) : QObject(parent) {}
    ~RemoteTransport() override = default;

    virtual State state() const = 0;
    virtual bool isConnected() const = 0;

    virtual int list(const QString &remoteDir) = 0;
    virtual int upload(const QString &localPath, const QString &remotePath) = 0;
    virtual int download(const QString &remotePath, const QString &localPath) = 0;
    virtual int makeDirectory(const QString &remotePath) = 0;
    virtual int removeFile(const QString &remotePath) = 0;
    virtual int removeDirectory(const QString &remotePath) = 0;
    virtual int rename(const QString &fromPath, const QString &toPath) = 0;
    virtual int requestSize(const QString &remotePath) = 0;
    virtual int ping() = 0;

    /// Cancels the transfer in flight (if any) and clears anything still queued.
    virtual void abortAll() = 0;

signals:
    void stateChanged(vsp::RemoteTransport::State state);
    void connected(const QString &greeting);
    void disconnected();

    /// Raised the moment the connection drops, before the in-flight request is
    /// failed, so a caller can tell "the device went away" apart from "the
    /// device refused this".
    void connectionLost();

    void commandFinished(int requestId, bool success, const QString &message);
    void listingReady(int requestId, const QString &remoteDir,
                      const QVector<vsp::RemoteEntry> &entries);
    void sizeReady(int requestId, const QString &remotePath, qint64 size);
    void transferProgress(int requestId, qint64 done, qint64 total);

    /// Raw trace for the collapsible details panel. Never shown in the main view.
    void logLine(const QString &line);
    void errorOccurred(const QString &message);
};

} // namespace vsp
