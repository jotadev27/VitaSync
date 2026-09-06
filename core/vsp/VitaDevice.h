#pragma once

#include "FtpClient.h"
#include "UsbTransport.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

QT_BEGIN_NAMESPACE
class QSettings;
class QTimer;
QT_END_NAMESPACE

namespace vsp {

class CompanionClient;
class DropStageModel;
class MetadataDb;
class RemoteBrowserModel;
class TransferQueue;

/// One object the whole UI talks to.
///
/// It owns the connection, the browser listing, the drop staging area and the
/// transfer queue, and it is the only place that knows how those fit together.
/// Keeping the wiring here is what lets the desktop and Android front ends be
/// nothing but layout.
class VitaDevice : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY connectionInfoChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY connectionInfoChanged)
    Q_PROPERTY(bool hostValid READ hostValid NOTIFY connectionInfoChanged)
    Q_PROPERTY(QStringList recentHosts READ recentHosts NOTIFY connectionInfoChanged)

    /// "ftp" (Wi-Fi, the default) or "usb". Two independent ways to reach the
    /// same device -- picking one never disables the other.
    Q_PROPERTY(QString connectionMode READ connectionMode WRITE setConnectionMode NOTIFY connectionModeChanged)
    Q_PROPERTY(QString usbRootPath READ usbRootPath WRITE setUsbRootPath NOTIFY usbRootPathChanged)
    Q_PROPERTY(QString usbRootPathDisplay READ usbRootPathDisplay NOTIFY usbRootPathChanged)
    Q_PROPERTY(QVariantList usbCandidates READ usbCandidates NOTIFY usbCandidatesChanged)

    /// What the top bar shows next to the status dot: "host" / ":port" over
    /// FTP, or the mounted volume's own name / "USB" over USB -- never an
    /// address for a connection that was never a socket in the first place.
    Q_PROPERTY(QString connectionSummary READ connectionSummary NOTIFY connectionInfoChanged)
    Q_PROPERTY(QString connectionDetail READ connectionDetail NOTIFY connectionInfoChanged)

    Q_PROPERTY(bool connected READ isConnected NOTIFY statusChanged)
    Q_PROPERTY(bool connecting READ isConnecting NOTIFY statusChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString statusTone READ statusTone NOTIFY statusChanged)
    Q_PROPERTY(QString deviceLabel READ deviceLabel NOTIFY statusChanged)
    Q_PROPERTY(bool companionAvailable READ companionAvailable NOTIFY statusChanged)

    Q_PROPERTY(QString currentPath READ currentPath NOTIFY locationChanged)
    Q_PROPERTY(QString mount READ mount WRITE setMount NOTIFY locationChanged)
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY locationChanged)
    Q_PROPERTY(QVariantList mountPoints READ mountPoints CONSTANT)
    Q_PROPERTY(QVariantList quickLocations READ quickLocations NOTIFY locationChanged)
    Q_PROPERTY(bool listing READ isListing NOTIFY locationChanged)

    Q_PROPERTY(QString downloadDir READ downloadDir WRITE setDownloadDir NOTIFY settingsChanged)
    Q_PROPERTY(QString downloadDirDisplay READ downloadDirDisplay NOTIFY settingsChanged)
    Q_PROPERTY(QString coverPackPath READ coverPackPath WRITE setCoverPackPath NOTIFY settingsChanged)
    Q_PROPERTY(QString coverPackPathDisplay READ coverPackPathDisplay NOTIFY settingsChanged)
    Q_PROPERTY(bool keepAwake READ keepAwake WRITE setKeepAwake NOTIFY settingsChanged)

    Q_PROPERTY(QStringList log READ log NOTIFY logChanged)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

    Q_PROPERTY(vsp::RemoteBrowserModel *browser READ browser CONSTANT)
    Q_PROPERTY(vsp::TransferQueue *transfers READ transfers CONSTANT)
    Q_PROPERTY(vsp::DropStageModel *drops READ drops CONSTANT)
    Q_PROPERTY(vsp::MetadataDb *metadata READ metadata CONSTANT)

public:
    explicit VitaDevice(QObject *parent = nullptr);
    ~VitaDevice() override;

    QString host() const { return m_host; }
    void setHost(const QString &host);
    int port() const { return m_port; }
    void setPort(int port);
    bool hostValid() const;
    QStringList recentHosts() const { return m_recentHosts; }

    QString connectionMode() const { return m_connectionMode; }
    void setConnectionMode(const QString &mode);
    QString usbRootPath() const { return m_usbRootPath; }
    void setUsbRootPath(const QString &rootPath);
    /// usbRootPath with the OS account's home directory collapsed to "~",
    /// for display -- a mounted-volume path routinely sits under
    /// /home/<user>/... or /run/media/<user>/..., which otherwise shows the
    /// account name to anyone looking at the app on someone else's machine.
    QString usbRootPathDisplay() const;
    QVariantList usbCandidates() const;
    QString connectionSummary() const;
    QString connectionDetail() const;

    bool isConnected() const;
    bool isConnecting() const;
    QString statusText() const { return m_statusText; }
    QString statusTone() const { return m_statusTone; }
    QString deviceLabel() const { return m_deviceLabel; }
    bool companionAvailable() const;

    QString currentPath() const { return m_currentPath; }
    QString mount() const { return m_mount; }
    void setMount(const QString &mount);
    QVariantList breadcrumb() const;
    QVariantList mountPoints() const;
    QVariantList quickLocations() const;
    bool isListing() const { return m_listRequestId != 0; }

    QString downloadDir() const { return m_downloadDir; }
    void setDownloadDir(const QString &directory);
    /// downloadDir with the home directory collapsed to "~" -- see
    /// usbRootPathDisplay() for why. Every actual file operation still uses
    /// downloadDir() itself, untouched.
    QString downloadDirDisplay() const;
    QString coverPackPath() const;
    void setCoverPackPath(const QString &directory);
    QString coverPackPathDisplay() const;
    bool keepAwake() const { return m_keepAwake; }
    void setKeepAwake(bool enabled);

    QStringList log() const { return m_log; }
    QString appVersion() const;

    RemoteBrowserModel *browser() const { return m_browser; }
    TransferQueue *transfers() const { return m_transfers; }
    DropStageModel *drops() const { return m_drops; }
    MetadataDb *metadata() const { return m_metadata; }

public slots:
    void connectToVita();
    /// Attaches to usbRootPath (or the sole detected candidate, if none was
    /// picked). The Wi-Fi/FTP connection, if any, is untouched.
    void connectUsb();
    void disconnectFromVita();

    /// Re-scans mounted volumes for the Vita USB signature. Safe to call at
    /// any time, connected or not.
    void refreshUsbCandidates();

    /// Restores both host and port from one "host:port" entry in
    /// recentHosts -- selecting a recent address has to bring back the port
    /// it actually worked on, not just the bare IP.
    void selectRecentHost(const QString &entry);
    void clearRecentHosts();

    void navigateTo(const QString &remotePath);
    void navigateUp();
    void refresh();
    void openRow(int row);

    /// Adds dropped files (paths or file:// URLs) to the staging area.
    int addDrops(const QStringList &paths);
    void clearDrops();
    /// Sends everything staged. Packages are verified and handed to install.
    void startStaged();

    void downloadSelection();
    void deleteSelection();
    void createFolder(const QString &name);
    void renameEntry(const QString &remotePath, const QString &newName);
    void moveSelectionTo(const QString &remoteDir);

    void openVitaShell();
    void rebootVita();
    void clearLog();

signals:
    void connectionInfoChanged();
    void connectionModeChanged();
    void usbRootPathChanged();
    void usbCandidatesChanged();
    void statusChanged();
    void locationChanged();
    void settingsChanged();
    void logChanged();
    /// Each line as it is appended, for mirroring the trace to a terminal.
    void logLineAdded(const QString &line);

    /// Short, non-blocking message for the status strip. \a tone is one of
    /// "info", "ok", "warn", "error".
    void notify(const QString &message, const QString &tone);

    /// An upload finished and verified; the Vita side needs a confirmation tap.
    void installReady(const QString &title, const QString &titleId,
                      const QString &remotePath);

    /// A folder is fully on the device -- a theme, or an already-unpacked
    /// game. \a nextStep names the step that finishes the job, which differs
    /// by what was sent.
    void folderInstallReady(const QString &title, const QString &remotePath,
                            const QString &nextStep);

    /// Work was just queued. The UI follows this to the Transfers screen, so
    /// pressing Start shows progress instead of an emptied drop list.
    void transfersStarted(int queued);

private:
    void setStatus(const QString &text, const QString &tone);
    void appendLog(const QString &line);
    void rememberHost(const QString &host, int port);
    void loadSettings();
    void saveSettings();
    void scheduleReconnect();
    /// What "the timer fired, try again" means depends on which mode is
    /// active: FTP retries the socket, USB retries attaching to the same
    /// (or, failing that, the sole detected) mount.
    void attemptReconnect();

    void onFtpStateChanged(RemoteTransport::State state);
    void onListingReady(int requestId, const QString &remoteDir,
                        const QVector<RemoteEntry> &entries);
    void onCommandFinished(int requestId, bool success, const QString &message);

    FtpClient *m_ftp = nullptr;
    UsbTransport *m_usb = nullptr;
    /// Whichever of the two is the live connection. Never null: it defaults
    /// to m_ftp, exactly like the single m_ftp pointer this replaced, so
    /// every call site that used to dereference m_ftp unconditionally still
    /// can.
    RemoteTransport *m_active = nullptr;
    /// Whichever transport TransferQueue/RemoteTreeScanner are currently
    /// wired to. Re-attaching is only done when this actually needs to
    /// change (a Wi-Fi/USB switch), never on a same-transport reconnect --
    /// that path is hardware-tested and must not gain a new moving part.
    RemoteTransport *m_attachedTransport = nullptr;
    CompanionClient *m_companion = nullptr;
    RemoteBrowserModel *m_browser = nullptr;
    TransferQueue *m_transfers = nullptr;
    DropStageModel *m_drops = nullptr;
    MetadataDb *m_metadata = nullptr;
    QSettings *m_settings = nullptr;
    QTimer *m_reconnect = nullptr;
    QTimer *m_keepAlive = nullptr;

    QString m_host;
    int m_port = 1337;
    QStringList m_recentHosts;

    QString m_connectionMode = QStringLiteral("ftp");
    QString m_usbRootPath;
    QVector<UsbTransport::Candidate> m_usbCandidates;
    QString m_statusText;
    QString m_statusTone = QStringLiteral("idle");
    QString m_deviceLabel;

    QString m_mount = QStringLiteral("ux0:");
    QString m_currentPath = QStringLiteral("/ux0:");
    QString m_pendingPath;
    int m_listRequestId = 0;

    QString m_downloadDir;
    bool m_keepAwake = true;

    QStringList m_log;
    bool m_userWantsConnection = false;
    int m_reconnectAttempts = 0;

    // Housekeeping commands whose failure should not raise an error banner.
    QList<int> m_quietRequests;
    QList<int> m_refreshRequests;
};

} // namespace vsp
