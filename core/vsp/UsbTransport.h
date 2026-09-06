#pragma once

#include "RemoteTransport.h"

#include <QFutureWatcher>
#include <QPromise>
#include <QQueue>
#include <QVector>

#include <optional>

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace vsp {

/// The USB implementation of `RemoteTransport`.
///
/// VitaShell's USB mode is not a protocol: it hands the raw memory-card
/// partition to the host as a standard USB Mass Storage device, which the OS
/// mounts exactly like a flash drive -- a real FAT/exFAT filesystem, no MTP,
/// no socket. "Connecting" over USB is therefore pointing at an already
/// mounted folder, and every operation below is plain `QDir`/`QFile` work.
/// See docs/usb.md for how this was confirmed against VitaShell's own source.
///
/// It is still written to the exact asynchronous, request-id contract
/// `FtpClient` established, so `TransferQueue` and `RemoteTreeScanner` run
/// completely unmodified against either transport: each request is handed to
/// a worker thread (`QtConcurrent`) and answered later by the matching
/// `RemoteTransport` signal, one request in flight at a time, mirroring the
/// single-connection discipline the FTP side needs for a different reason.
class UsbTransport : public RemoteTransport
{
    Q_OBJECT

public:
    /// A mounted removable volume that looks like a Vita memory card.
    struct Candidate
    {
        QString rootPath;
        QString label;
        qint64 bytesTotal = 0;
        qint64 bytesFree = 0;
    };

    explicit UsbTransport(QObject *parent = nullptr);
    ~UsbTransport() override;

    /// Scans the volumes the OS currently has mounted for the Vita signature
    /// (see docs/usb.md). A plain function of the filesystem.
    static QVector<Candidate> detectCandidates();
    /// Same scan, but over an explicit list of roots rather than whatever the
    /// OS happens to have mounted -- what the tests use in place of a real
    /// USB device.
    static QVector<Candidate> detectCandidates(const QStringList &rootsToCheck);

    /// True when \a rootPath carries the Vita signature both id.dat and a
    /// VitaShell install folder. Exposed so the UI can label an unverified
    /// manual folder pick differently from a confirmed one.
    static bool looksLikeVitaVolume(const QString &rootPath);

    State state() const override { return m_state; }
    bool isConnected() const override { return m_state == State::Ready || m_state == State::Busy; }

    QString rootPath() const { return m_rootPath; }
    bool isVerified() const { return m_verified; }

    /// Points the transport at an already-mounted folder standing in for
    /// ux0:. Fails synchronously (returns false, emits errorOccurred) only if
    /// the path is not a readable directory -- an unrecognised folder is
    /// still accepted, for the same reason the game-identification path
    /// never blocks on an unmatched title: a wrong guess should be visible,
    /// not fatal. Callers read isVerified() to tell the two apart.
    bool attachToMount(const QString &rootPath);
    void detachFromMount();

    int list(const QString &remoteDir) override;
    int upload(const QString &localPath, const QString &remotePath) override;
    int download(const QString &remotePath, const QString &localPath) override;
    int makeDirectory(const QString &remotePath) override;
    int removeFile(const QString &remotePath) override;
    int removeDirectory(const QString &remotePath) override;
    int rename(const QString &fromPath, const QString &toPath) override;
    int requestSize(const QString &remotePath) override;
    int ping() override;

    void abortAll() override;

private:
    enum class Op {
        None, List, Upload, Download, Mkdir, RemoveFile, DeleteDir, Rename, Size
    };

    struct Request
    {
        int id = 0;
        Op op = Op::None;
        QString primaryPath;    // remote-style, e.g. "/ux0:/vpk"
        QString secondaryPath;  // rename target, remote-style
        QString localPath;
    };

    /// What a worker reports back, translated into a RemoteTransport signal
    /// on the GUI thread once the future finishes.
    struct Outcome
    {
        bool success = false;
        QString message;
        QVector<RemoteEntry> entries;   // List
        qint64 size = -1;               // Size
    };

    void enqueue(const Request &request);
    void pumpQueue();
    void beginRequest(const Request &request);
    /// A request that was never handed to a worker at all -- primaryPath
    /// didn't map onto ux0:, or the request was empty -- answered
    /// immediately with no mount check. On real hardware a mounted USB
    /// volume can be genuinely slow to stat, so a check that can never
    /// succeed (the path was invalid before any I/O was attempted) must
    /// never pay that cost; see docs/usb.md.
    void rejectRequest(const QString &message);
    void finishRequest(const Outcome &outcome);
    /// The tail of finishRequest() once it is known whether the device is
    /// still there: notifies the caller and pumps the next request.
    void completeRequest(const Outcome &outcome);
    void setState(State state);

    QString mapToLocal(const QString &remotePath, QString *error) const;

    static Outcome runList(QString localDir, QString remoteDir);
    static void runCopyFile(QPromise<Outcome> &promise, QString fromPath, QString toPath);
    static Outcome runMkdir(QString localDir);
    static Outcome runRemoveFile(QString localPath);
    static Outcome runRemoveDirectory(QString localPath);
    static Outcome runRename(QString fromLocal, QString toLocal);
    static Outcome runSize(QString localPath, QString remotePath);

    void checkStillMounted();
    /// One place a lost mount is dealt with -- mirrors FtpClient's
    /// handleConnectionLoss(): fail the in-flight request (if any) after
    /// telling the caller the device went away, so TransferQueue puts the job
    /// back in the queue instead of just failing it.
    void handleMountLoss(const QString &message);
    void onWatcherFinished();
    /// Runs `QStorageInfo::refresh()` for m_rootPath on a worker thread --
    /// stat-ing a real, physically slow USB device must never block the GUI
    /// thread, which the periodic health check and a failed request used to
    /// do directly. \a forRequest is set when this check is gating a
    /// specific request's completion (see finishRequest()); left default for
    /// the periodic timer's own health check, which has no request to
    /// resume afterwards.
    void checkMountAsync(std::optional<Outcome> forRequest = std::nullopt);
    void onMountCheckFinished();

    QString m_rootPath;
    bool m_verified = false;

    State m_state = State::Disconnected;

    QQueue<Request> m_queue;
    Request m_current;
    bool m_busy = false;
    /// False once a request has been cancelled or failed via handleMountLoss,
    /// so the worker's eventual (ignored) completion cannot double-report it.
    bool m_expectingFinish = false;
    qint64 m_currentTotal = 0;

    QFutureWatcher<Outcome> *m_watcher = nullptr;
    QTimer *m_mountWatch = nullptr;

    QFutureWatcher<bool> *m_mountCheckWatcher = nullptr;
    bool m_mountCheckInFlight = false;
    /// Set only while a mount check is gating a specific failed request's
    /// completion; empty when a check is just the periodic health poll.
    std::optional<Outcome> m_pendingOutcome;

    int m_nextRequestId = 1;
};

} // namespace vsp
