#include "VitaDevice.h"

#include "CompanionClient.h"
#include "DropStageModel.h"
#include "MetadataDb.h"
#include "PathUtils.h"
#include "RemoteBrowserModel.h"
#include "TransferQueue.h"
#include "Version.h"
#include "VitaPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

namespace vsp {
namespace {

constexpr int kMaxLogLines = 400;
constexpr int kMaxRecentHosts = 6;
// VitaShell's own Title ID, used to bring it to the front for an install.
const QLatin1String kVitaShellTitleId("VITASHELL");

QString toLocalDirectory(const QString &pathOrUrl)
{
    QString value = pathOrUrl;
    if (value.startsWith(QLatin1String("file:")))
        value = QUrl(value).toLocalFile();
    // The fields that go through here show a "~"-collapsed path (see
    // connectionSummary()'s siblings below); typing one back in -- or simply
    // leaving it as the field showed it -- has to resolve to the same real
    // directory it displayed, not a literal "~" nothing on disk answers to.
    value = path::expandUserHome(value);
    return QDir::cleanPath(value);
}

// A single-file download lands under its own type folder rather than loose
// in the download folder, the same way the Vita side keeps games, videos and
// photos apart -- so a pile of downloaded VPKs doesn't end up mixed in with
// savedata and screenshots. Anything without a dedicated folder still lands
// directly in the download folder, unchanged.
QString localDownloadDirFor(const QString &baseDir, const QString &fileName)
{
    if (fileName.endsWith(QLatin1String(".vpk"), Qt::CaseInsensitive))
        return QDir(baseDir).filePath(QStringLiteral("vpk"));
    return baseDir;
}

} // namespace

VitaDevice::VitaDevice(QObject *parent)
    : QObject(parent)
    , m_ftp(new FtpClient(this))
    , m_usb(new UsbTransport(this))
    , m_companion(new CompanionClient(this))
    , m_browser(new RemoteBrowserModel(this))
    , m_transfers(new TransferQueue(this))
    , m_drops(new DropStageModel(this))
    , m_metadata(new MetadataDb(this))
    , m_settings(new QSettings(QStringLiteral("VitaSync"), QStringLiteral("VitaSync"), this))
    , m_reconnect(new QTimer(this))
    , m_keepAlive(new QTimer(this))
{
    m_active = m_ftp;
    m_attachedTransport = m_ftp;
    m_transfers->attach(m_ftp, m_companion);
    m_drops->setMetadataDb(m_metadata);

    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, [this] {
        if (m_userWantsConnection && !isConnected())
            attemptReconnect();
    });

    // A Vita left alone will drop an idle FTP session; a periodic NOOP is
    // cheaper than making the user reconnect every few minutes.
    m_keepAlive->setInterval(45000);
    connect(m_keepAlive, &QTimer::timeout, this, [this] {
        if (m_ftp->state() == FtpClient::State::Ready)
            m_quietRequests.append(m_ftp->ping());
    });

    connect(m_ftp, &FtpClient::stateChanged, this, &VitaDevice::onFtpStateChanged);
    connect(m_ftp, &FtpClient::listingReady, this, &VitaDevice::onListingReady);
    connect(m_ftp, &FtpClient::commandFinished, this, &VitaDevice::onCommandFinished);
    connect(m_ftp, &FtpClient::logLine, this, &VitaDevice::appendLog);
    connect(m_ftp, &FtpClient::errorOccurred, this, [this](const QString &message) {
        setStatus(message, QStringLiteral("error"));
        emit notify(message, QStringLiteral("error"));
    });
    connect(m_ftp, &FtpClient::connected, this, [this](const QString &greeting) {
        m_reconnectAttempts = 0;
        m_deviceLabel = greeting.isEmpty() ? m_host : greeting;
        setStatus(QStringLiteral("Connected"), QStringLiteral("ok"));
        rememberHost(m_host, m_port);
        m_keepAlive->start();

        m_companion->probe(m_host);
        navigateTo(QStringLiteral("/") + m_mount);
        m_transfers->start();
    });
    connect(m_ftp, &FtpClient::disconnected, this, [this] {
        m_keepAlive->stop();
        m_deviceLabel.clear();
        if (m_userWantsConnection) {
            scheduleReconnect();
        } else {
            setStatus(QStringLiteral("Disconnected"), QStringLiteral("idle"));
        }
    });

    // USB shares the same listing/command plumbing as FTP -- both funnel
    // through the same slots, which key off request ids rather than which
    // transport issued them, so nothing here needs to know which is live.
    connect(m_usb, &RemoteTransport::stateChanged, this, &VitaDevice::onFtpStateChanged);
    connect(m_usb, &RemoteTransport::listingReady, this, &VitaDevice::onListingReady);
    connect(m_usb, &RemoteTransport::commandFinished, this, &VitaDevice::onCommandFinished);
    connect(m_usb, &RemoteTransport::logLine, this, &VitaDevice::appendLog);
    connect(m_usb, &RemoteTransport::errorOccurred, this, [this](const QString &message) {
        setStatus(message, QStringLiteral("error"));
        emit notify(message, QStringLiteral("error"));
    });
    connect(m_usb, &RemoteTransport::connected, this, [this](const QString &greeting) {
        m_reconnectAttempts = 0;
        m_deviceLabel = greeting;
        // Not a network session, so there is nothing here that plays the
        // part of vitacompanion's probe(): the command server it would
        // answer on is unreachable over a mass-storage cable in the first
        // place, and companionAvailable() correctly stays false.
        setStatus(QStringLiteral("USB Connected"),
                  m_usb->isVerified() ? QStringLiteral("ok") : QStringLiteral("warn"));
        if (!m_usb->isVerified()) {
            emit notify(QStringLiteral("This folder doesn't look like a Vita card — continuing anyway"),
                        QStringLiteral("warn"));
        }
        navigateTo(QStringLiteral("/ux0:"));
        m_transfers->start();
    });
    connect(m_usb, &RemoteTransport::disconnected, this, [this] {
        m_deviceLabel.clear();
        if (m_userWantsConnection) {
            scheduleReconnect();
        } else {
            setStatus(QStringLiteral("USB Disconnected"), QStringLiteral("idle"));
        }
    });

    connect(m_companion, &CompanionClient::availabilityChanged, this, [this](bool available) {
        if (available && m_keepAwake)
            m_companion->keepAwake(true);
        emit statusChanged();
    });
    connect(m_companion, &CompanionClient::logLine, this, &VitaDevice::appendLog);

    connect(m_transfers, &TransferQueue::logLine, this, &VitaDevice::appendLog);
    connect(m_transfers, &TransferQueue::installReady, this,
            [this](int, const QString &title, const QString &titleId, const QString &remotePath) {
                emit installReady(title, titleId, remotePath);
                emit notify(QStringLiteral("%1 verified on device").arg(title),
                            QStringLiteral("ok"));
            });
    connect(m_transfers, &TransferQueue::folderInstallReady, this,
            [this](int, const QString &title, const QString &remotePath,
                   const QString &nextStep) {
                emit folderInstallReady(title, remotePath, nextStep);
                emit notify(QStringLiteral("%1 sent to the Vita").arg(title),
                            QStringLiteral("ok"));
            });
    connect(m_transfers, &TransferQueue::jobPostponed, this,
            [this](int, const QString &title) {
                // Not an error: the job is back in the queue and the
                // connection is already being retried.
                emit notify(QStringLiteral("%1 paused — waiting for the Vita").arg(title),
                            QStringLiteral("warn"));
            });
    connect(m_transfers, &TransferQueue::jobFinished, this,
            [this](int, bool success, const QString &message) {
                if (!success)
                    emit notify(message, QStringLiteral("error"));
                // A completed transfer changes what is on screen.
                if (success && isConnected())
                    refresh();
            });

    loadSettings();
}

VitaDevice::~VitaDevice()
{
    saveSettings();
}

QString VitaDevice::appVersion() const
{
    return vsp::versionString();
}

// --- settings -------------------------------------------------------------

void VitaDevice::loadSettings()
{
    m_host = m_settings->value(QStringLiteral("connection/host")).toString();
    m_port = m_settings->value(QStringLiteral("connection/port"), 1337).toInt();
    if (!path::isValidPort(m_port))
        m_port = 1337;
    m_recentHosts = m_settings->value(QStringLiteral("connection/recent")).toStringList();
    m_mount = m_settings->value(QStringLiteral("browser/mount"), QStringLiteral("ux0:")).toString();
    m_currentPath = QStringLiteral("/") + m_mount;
    m_keepAwake = m_settings->value(QStringLiteral("device/keepAwake"), true).toBool();

    m_downloadDir = m_settings->value(QStringLiteral("transfer/downloadDir")).toString();
    if (m_downloadDir.isEmpty()) {
        m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (m_downloadDir.isEmpty())
            m_downloadDir = QDir::homePath();
        m_downloadDir = QDir(m_downloadDir).filePath(QStringLiteral("VitaSync"));
    }

    // The metadata index ships beside the binary; fall back to the source tree
    // layout so a development build finds it too.
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/covers/titles.json"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/covers/titles.json"),
        QStringLiteral(":/assets/covers/titles.json")
    };
    for (const QString &candidate : candidates) {
        if (m_metadata->load(candidate)) {
            appendLog(QStringLiteral("metadata: %1 titles from %2")
                          .arg(m_metadata->titleCount())
                          .arg(candidate));
            break;
        }
    }

    const QString coverPack = m_settings->value(QStringLiteral("metadata/coverPack")).toString();
    if (!coverPack.isEmpty())
        m_metadata->setCoverPackPath(coverPack);

    m_drops->setMount(m_mount);
    setStatus(QStringLiteral("Not connected"), QStringLiteral("idle"));

    emit connectionInfoChanged();
    emit locationChanged();
    emit settingsChanged();
}

void VitaDevice::saveSettings()
{
    m_settings->setValue(QStringLiteral("connection/host"), m_host);
    m_settings->setValue(QStringLiteral("connection/port"), m_port);
    m_settings->setValue(QStringLiteral("connection/recent"), m_recentHosts);
    m_settings->setValue(QStringLiteral("browser/mount"), m_mount);
    m_settings->setValue(QStringLiteral("transfer/downloadDir"), m_downloadDir);
    m_settings->setValue(QStringLiteral("metadata/coverPack"), m_metadata->coverPackPath());
    m_settings->setValue(QStringLiteral("device/keepAwake"), m_keepAwake);
    // Note: no credential is ever written here. If a device asks for a
    // password it lives in memory for the session and nowhere else.
}

// --- connection -----------------------------------------------------------

void VitaDevice::setHost(const QString &host)
{
    const QString trimmed = host.trimmed();
    if (m_host == trimmed)
        return;
    m_host = trimmed;
    emit connectionInfoChanged();
}

void VitaDevice::setPort(int port)
{
    if (m_port == port)
        return;
    m_port = port;
    emit connectionInfoChanged();
}

bool VitaDevice::hostValid() const
{
    return path::isValidIPv4(m_host) && path::isValidPort(m_port);
}

void VitaDevice::setConnectionMode(const QString &mode)
{
    const QString normalized = mode == QLatin1String("usb") ? mode : QStringLiteral("ftp");
    if (m_connectionMode == normalized)
        return;
    // Switching mode while linked would leave two connections half-alive;
    // the UI only ever shows one Connect/Disconnect button, so drop the old
    // one first, exactly as if the user had pressed Disconnect.
    if (isConnected())
        disconnectFromVita();
    m_connectionMode = normalized;
    emit connectionModeChanged();
    // connectionSummary()/connectionDetail() both depend on the mode too.
    emit connectionInfoChanged();
}

void VitaDevice::setUsbRootPath(const QString &rootPath)
{
    const QString cleaned = toLocalDirectory(rootPath);
    if (m_usbRootPath == cleaned)
        return;
    m_usbRootPath = cleaned;
    emit usbRootPathChanged();
    emit connectionInfoChanged();
}

QString VitaDevice::connectionSummary() const
{
    // Never an address for a link that was never a socket: USB is a mounted
    // folder, so the top bar names the volume, not a host that does not
    // exist. See docs/usb.md.
    if (m_connectionMode != QLatin1String("usb"))
        return m_host;
    const QString name = QFileInfo(m_usbRootPath).fileName();
    return name.isEmpty() ? m_usbRootPath : name;
}

QString VitaDevice::connectionDetail() const
{
    return m_connectionMode == QLatin1String("usb")
               ? QStringLiteral("USB")
               : QStringLiteral(":%1").arg(m_port);
}

QString VitaDevice::usbRootPathDisplay() const
{
    return path::collapseUserHome(m_usbRootPath);
}

QVariantList VitaDevice::usbCandidates() const
{
    QVariantList list;
    for (const UsbTransport::Candidate &candidate : m_usbCandidates) {
        list.append(QVariantMap {
            { QStringLiteral("rootPath"), candidate.rootPath },
            { QStringLiteral("label"), candidate.label },
            { QStringLiteral("verified"), UsbTransport::looksLikeVitaVolume(candidate.rootPath) },
            { QStringLiteral("sizeText"), path::humanSize(candidate.bytesTotal) },
            { QStringLiteral("freeText"), path::humanSize(candidate.bytesFree) }
        });
    }
    return list;
}

void VitaDevice::refreshUsbCandidates()
{
    m_usbCandidates = UsbTransport::detectCandidates();
    emit usbCandidatesChanged();
}

bool VitaDevice::isConnected() const
{
    return m_active && m_active->isConnected();
}

bool VitaDevice::isConnecting() const
{
    return (m_active && m_active->state() == RemoteTransport::State::Connecting)
        || m_reconnect->isActive();
}

bool VitaDevice::companionAvailable() const
{
    return m_companion->isAvailable();
}

void VitaDevice::connectToVita()
{
    if (!hostValid()) {
        const QString message = path::isValidIPv4(m_host)
                                    ? QStringLiteral("Port must be between 1 and 65535")
                                    : QStringLiteral("Enter a valid IPv4 address");
        setStatus(message, QStringLiteral("error"));
        emit notify(message, QStringLiteral("error"));
        return;
    }

    m_userWantsConnection = true;
    m_connectionMode = QStringLiteral("ftp");
    m_active = m_ftp;
    // TransferQueue/RemoteTreeScanner run against whichever transport is
    // live; only re-point them when that is actually changing (a mode
    // switch), so a same-transport reconnect behaves exactly as before.
    if (m_attachedTransport != m_active) {
        m_attachedTransport = m_active;
        m_transfers->attach(m_active, m_companion);
    }
    m_reconnect->stop();
    setStatus(QStringLiteral("Connecting to %1").arg(m_host), QStringLiteral("busy"));
    m_ftp->connectToDevice(m_host, static_cast<quint16>(m_port));
}

void VitaDevice::connectUsb()
{
    if (m_usbRootPath.isEmpty()) {
        // No manual pick: attach to the sole detected candidate. Two or more
        // is left for the user to choose between in the UI.
        refreshUsbCandidates();
        if (m_usbCandidates.size() == 1)
            setUsbRootPath(m_usbCandidates.first().rootPath);
    }
    if (m_usbRootPath.isEmpty()) {
        const QString message = m_usbCandidates.isEmpty()
            ? QStringLiteral("No USB-connected Vita found. Plug in the cable and pick USB in VitaShell.")
            : QStringLiteral("More than one USB drive matched — pick one below.");
        setStatus(message, QStringLiteral("error"));
        emit notify(message, QStringLiteral("warn"));
        return;
    }

    m_userWantsConnection = true;
    m_connectionMode = QStringLiteral("usb");
    m_active = m_usb;
    if (m_attachedTransport != m_active) {
        m_attachedTransport = m_active;
        m_transfers->attach(m_active, m_companion);
    }
    m_reconnect->stop();
    setStatus(QStringLiteral("Connecting over USB"), QStringLiteral("busy"));
    m_usb->attachToMount(m_usbRootPath);
}

void VitaDevice::disconnectFromVita()
{
    m_userWantsConnection = false;
    m_reconnect->stop();
    m_keepAlive->stop();
    if (m_keepAwake && m_companion->isAvailable())
        m_companion->keepAwake(false);

    if (m_connectionMode == QLatin1String("usb"))
        m_usb->detachFromMount();
    else
        m_ftp->disconnectFromDevice();

    m_browser->setEntries({});
    setStatus(m_connectionMode == QLatin1String("usb") ? QStringLiteral("USB Disconnected")
                                                        : QStringLiteral("Disconnected"),
              QStringLiteral("idle"));
}

void VitaDevice::scheduleReconnect()
{
    if (!m_userWantsConnection)
        return;
    const bool usb = m_connectionMode == QLatin1String("usb");
    if (m_reconnectAttempts >= 5) {
        m_userWantsConnection = false;
        setStatus(usb ? QStringLiteral("Lost the USB connection") : QStringLiteral("Lost connection to %1").arg(m_host),
                  QStringLiteral("error"));
        emit notify(usb ? QStringLiteral("Could not find the Vita over USB. Check the cable and VitaShell's USB mode.")
                        : QStringLiteral("Could not reach the Vita. Check Wi-Fi and VitaShell's FTP."),
                    QStringLiteral("error"));
        return;
    }

    ++m_reconnectAttempts;
    // Back off so a sleeping Vita is not hammered: 2s, 4s, 8s, 16s, 30s.
    const int delayMs = qMin(2000 << (m_reconnectAttempts - 1), 30000);
    setStatus(QStringLiteral("Reconnecting (%1/5)").arg(m_reconnectAttempts),
              QStringLiteral("busy"));
    m_reconnect->start(delayMs);
}

void VitaDevice::attemptReconnect()
{
    if (m_connectionMode == QLatin1String("usb"))
        connectUsb();
    else
        connectToVita();
}

void VitaDevice::onFtpStateChanged(RemoteTransport::State state)
{
    Q_UNUSED(state)
    emit statusChanged();
}

// --- browsing -------------------------------------------------------------

void VitaDevice::setMount(const QString &mount)
{
    if (m_mount == mount)
        return;
    m_mount = mount;
    m_drops->setMount(mount);
    emit locationChanged();
    if (isConnected())
        navigateTo(QStringLiteral("/") + mount);
}

void VitaDevice::navigateTo(const QString &remotePath)
{
    if (!isConnected())
        return;

    const QString target = path::normalizeRemote(remotePath);
    m_pendingPath = target;
    m_listRequestId = m_active->list(target);
    emit locationChanged();
}

void VitaDevice::navigateUp()
{
    if (m_currentPath == QLatin1String("/"))
        return; // nothing above the true root
    if (path::isMountRoot(m_currentPath)) {
        // One level above a mount root ("/ux0:", "/ur0:", ...) is the true
        // filesystem root -- the same target the sidebar's "Root" entry
        // uses, and reached the same way, so the two are never inconsistent
        // about what "the top" means. See docs/browsing.md.
        navigateTo(QStringLiteral("/"));
        return;
    }
    navigateTo(path::parentOfRemote(m_currentPath));
}

void VitaDevice::refresh()
{
    if (!isConnected())
        return;
    m_pendingPath = m_currentPath;
    m_listRequestId = m_active->list(m_currentPath);
    // Deliberately not tracked as a "refresh request": that set is for the
    // mutating commands whose completion should trigger a listing. Adding the
    // listing itself would make every refresh schedule another one, forever.
    emit locationChanged();
}

void VitaDevice::openRow(int row)
{
    const QVariantMap item = m_browser->itemAt(row);
    if (item.isEmpty())
        return;
    if (item.value(QStringLiteral("isDirectory")).toBool())
        navigateTo(item.value(QStringLiteral("path")).toString());
}

void VitaDevice::onListingReady(int requestId, const QString &remoteDir,
                                const QVector<RemoteEntry> &entries)
{
    if (requestId != m_listRequestId)
        return;

    m_listRequestId = 0;
    m_currentPath = remoteDir;

    const QString mount = path::mountOf(remoteDir);
    if (!mount.isEmpty() && mount != m_mount) {
        m_mount = mount;
        m_drops->setMount(mount);
    }

    m_browser->setEntries(entries);
    emit locationChanged();
}

QVariantList VitaDevice::breadcrumb() const
{
    QVariantList crumbs;
    QStringList parts = m_currentPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);

    QString accumulated;
    for (const QString &part : std::as_const(parts)) {
        accumulated += QLatin1Char('/') + part;
        crumbs.append(QVariantMap {
            { QStringLiteral("label"), part },
            { QStringLiteral("path"), accumulated }
        });
    }
    return crumbs;
}

QVariantList VitaDevice::mountPoints() const
{
    QVariantList list;
    const QVector<VitaLocation> points = vitapaths::mountPoints();
    for (const VitaLocation &point : points) {
        list.append(QVariantMap {
            { QStringLiteral("id"), point.id },
            { QStringLiteral("label"), point.label },
            { QStringLiteral("path"), point.path },
            { QStringLiteral("iconName"), point.iconName },
            { QStringLiteral("mount"), point.id + QLatin1Char(':') }
        });
    }
    return list;
}

QVariantList VitaDevice::quickLocations() const
{
    QVariantList list;
    const QVector<VitaLocation> locations = vitapaths::quickLocations(m_mount);
    for (const VitaLocation &location : locations) {
        list.append(QVariantMap {
            { QStringLiteral("id"), location.id },
            { QStringLiteral("label"), location.label },
            { QStringLiteral("path"), location.path },
            { QStringLiteral("iconName"), location.iconName },
            { QStringLiteral("current"), location.path == m_currentPath }
        });
    }
    return list;
}

// --- drops and transfers --------------------------------------------------

int VitaDevice::addDrops(const QStringList &paths)
{
    const int added = m_drops->addPaths(paths);
    if (added == 0) {
        emit notify(QStringLiteral("Nothing new to add"), QStringLiteral("warn"));
        return 0;
    }
    emit notify(added == 1 ? QStringLiteral("1 item ready")
                           : QStringLiteral("%1 items ready").arg(added),
                QStringLiteral("info"));
    return added;
}

void VitaDevice::clearDrops()
{
    m_drops->clear();
}

void VitaDevice::startStaged()
{
    if (!isConnected()) {
        emit notify(QStringLiteral("Connect to the Vita first"), QStringLiteral("warn"));
        return;
    }

    const QList<PackageInfo> items = m_drops->items();
    const QStringList destinations = m_drops->destinations();
    if (items.isEmpty())
        return;

    if (m_keepAwake && m_companion->isAvailable())
        m_companion->keepAwake(true);

    int queued = 0;
    for (int i = 0; i < items.size(); ++i) {
        const PackageInfo &info = items.at(i);
        if (!info.valid)
            continue;

        const QString destination = destinations.value(i);
        if (destination.isEmpty())
            continue;

        if (info.isDirectoryPayload())
            m_transfers->enqueueFolderInstall(info, destination);
        else
            m_transfers->enqueueUpload(info, destination, info.isInstallable());
        ++queued;
    }

    if (queued == 0) {
        emit notify(QStringLiteral("Nothing in the list can be sent"), QStringLiteral("warn"));
        return;
    }

    m_drops->clear();
    m_transfers->start();
    emit notify(QStringLiteral("%1 queued").arg(queued), QStringLiteral("ok"));
    emit transfersStarted(queued);
}

void VitaDevice::downloadSelection()
{
    const QVariantList selection = m_browser->selectedItems();
    if (selection.isEmpty()) {
        emit notify(QStringLiteral("Select something first"), QStringLiteral("warn"));
        return;
    }

    QDir target(m_downloadDir);
    if (!target.exists() && !target.mkpath(QStringLiteral("."))) {
        emit notify(QStringLiteral("Cannot write to %1").arg(m_downloadDir),
                    QStringLiteral("error"));
        return;
    }

    int queuedFiles = 0;
    int queuedFolders = 0;
    int rejected = 0;

    for (const QVariant &value : selection) {
        const QVariantMap item = value.toMap();
        const QString remotePath = item.value(QStringLiteral("path")).toString();
        const QString name = item.value(QStringLiteral("name")).toString();

        if (item.value(QStringLiteral("isDirectory")).toBool()) {
            // The folder is walked first, then mirrored file by file.
            if (m_transfers->enqueueFolderDownload(remotePath, m_downloadDir, name) > 0)
                ++queuedFolders;
            else
                ++rejected;
            continue;
        }

        const QString localDir = localDownloadDirFor(m_downloadDir, name);
        if (localDir != m_downloadDir)
            QDir().mkpath(localDir);

        const int id = m_transfers->enqueueDownload(remotePath,
                                                    item.value(QStringLiteral("size")).toLongLong(),
                                                    localDir, name);
        if (id > 0)
            ++queuedFiles;
        else
            ++rejected;
    }

    m_transfers->start();

    const int queued = queuedFiles + queuedFolders;
    if (queued == 0) {
        emit notify(QStringLiteral("Nothing could be queued"), QStringLiteral("error"));
        return;
    }

    QString summary;
    if (queuedFolders > 0 && queuedFiles > 0) {
        summary = QStringLiteral("%1 file(s) and %2 folder(s) queued")
                      .arg(queuedFiles).arg(queuedFolders);
    } else if (queuedFolders > 0) {
        summary = QStringLiteral("%1 folder(s) queued").arg(queuedFolders);
    } else {
        summary = QStringLiteral("%1 queued").arg(queuedFiles);
    }
    if (rejected > 0)
        summary += QStringLiteral(" · %1 skipped").arg(rejected);

    emit notify(summary, rejected > 0 ? QStringLiteral("warn") : QStringLiteral("ok"));
}

void VitaDevice::deleteSelection()
{
    const QVariantList selection = m_browser->selectedItems();
    if (selection.isEmpty())
        return;

    int issued = 0;
    for (const QVariant &value : selection) {
        const QVariantMap item = value.toMap();
        const QString remotePath = item.value(QStringLiteral("path")).toString();
        if (vitapaths::isProtectedPath(remotePath))
            continue;

        if (item.value(QStringLiteral("isDirectory")).toBool())
            m_refreshRequests.append(m_active->removeDirectory(remotePath));
        else
            m_refreshRequests.append(m_active->removeFile(remotePath));
        ++issued;
    }

    if (issued == 0) {
        emit notify(QStringLiteral("Those are system folders — not deleted"),
                    QStringLiteral("warn"));
        return;
    }
    emit notify(QStringLiteral("Deleting %1").arg(issued), QStringLiteral("info"));
}

void VitaDevice::createFolder(const QString &name)
{
    const QString safe = path::sanitizeFileName(name);
    if (safe.isEmpty() || !isConnected())
        return;
    m_refreshRequests.append(m_active->makeDirectory(path::joinRemote(m_currentPath, safe)));
}

void VitaDevice::renameEntry(const QString &remotePath, const QString &newName)
{
    const QString safe = path::sanitizeFileName(newName);
    if (safe.isEmpty() || !isConnected())
        return;

    const QString target = path::joinRemote(path::parentOfRemote(remotePath), safe);
    if (target == path::normalizeRemote(remotePath))
        return;
    m_refreshRequests.append(m_active->rename(remotePath, target));
}

void VitaDevice::moveSelectionTo(const QString &remoteDir)
{
    const QStringList paths = m_browser->selectedPaths();
    if (paths.isEmpty() || !isConnected())
        return;

    const QString destination = path::normalizeRemote(remoteDir);
    for (const QString &source : paths) {
        if (path::parentOfRemote(source) == destination)
            continue;
        if (vitapaths::isProtectedPath(source))
            continue;
        const QString target = path::joinRemote(destination, path::baseNameOfRemote(source));
        m_refreshRequests.append(m_active->rename(source, target));
    }
    emit notify(QStringLiteral("Moving %1 to %2").arg(paths.size()).arg(destination),
                QStringLiteral("info"));
}

void VitaDevice::onCommandFinished(int requestId, bool success, const QString &message)
{
    if (m_quietRequests.removeOne(requestId))
        return;

    const bool wasRefresh = m_refreshRequests.removeOne(requestId);

    if (requestId == m_listRequestId && !success) {
        m_listRequestId = 0;
        setStatus(message, QStringLiteral("error"));
        emit locationChanged();
        return;
    }

    if (wasRefresh) {
        if (!success)
            emit notify(message, QStringLiteral("error"));
        else if (isConnected())
            refresh();
    }
}

// --- device commands ------------------------------------------------------

void VitaDevice::openVitaShell()
{
    if (!m_companion->isAvailable()) {
        emit notify(QStringLiteral("vitacompanion plugin not detected"), QStringLiteral("warn"));
        return;
    }
    m_companion->launchTitle(kVitaShellTitleId);
    emit notify(QStringLiteral("Opening VitaShell on the device"), QStringLiteral("info"));
}

void VitaDevice::rebootVita()
{
    if (!m_companion->isAvailable()) {
        emit notify(QStringLiteral("vitacompanion plugin not detected"), QStringLiteral("warn"));
        return;
    }
    m_companion->reboot();
}

void VitaDevice::setKeepAwake(bool enabled)
{
    if (m_keepAwake == enabled)
        return;
    m_keepAwake = enabled;
    if (m_companion->isAvailable())
        m_companion->keepAwake(enabled);
    emit settingsChanged();
}

void VitaDevice::setDownloadDir(const QString &directory)
{
    const QString cleaned = toLocalDirectory(directory);
    if (cleaned.isEmpty() || cleaned == m_downloadDir)
        return;
    m_downloadDir = cleaned;
    emit settingsChanged();
}

QString VitaDevice::downloadDirDisplay() const
{
    return path::collapseUserHome(m_downloadDir);
}

QString VitaDevice::coverPackPath() const
{
    return m_metadata->coverPackPath();
}

QString VitaDevice::coverPackPathDisplay() const
{
    return path::collapseUserHome(coverPackPath());
}

void VitaDevice::setCoverPackPath(const QString &directory)
{
    m_metadata->setCoverPackPath(toLocalDirectory(directory));
    emit settingsChanged();
    emit notify(m_metadata->coverCount() > 0
                    ? QStringLiteral("%1 covers indexed").arg(m_metadata->coverCount())
                    : QStringLiteral("No cover images found in that folder"),
                m_metadata->coverCount() > 0 ? QStringLiteral("ok") : QStringLiteral("warn"));
}

// --- status and log -------------------------------------------------------

void VitaDevice::setStatus(const QString &text, const QString &tone)
{
    if (m_statusText == text && m_statusTone == tone)
        return;
    m_statusText = text;
    m_statusTone = tone;
    emit statusChanged();
}

void VitaDevice::appendLog(const QString &line)
{
    m_log.append(line);
    while (m_log.size() > kMaxLogLines)
        m_log.removeFirst();
    emit logChanged();
    emit logLineAdded(line);
}

void VitaDevice::clearLog()
{
    m_log.clear();
    emit logChanged();
}

void VitaDevice::rememberHost(const QString &host, int port)
{
    if (host.isEmpty())
        return;
    // Stored as one "host:port" entry so re-selecting it restores the port
    // it actually worked on, not just the bare address -- a Vita on a
    // non-default FTP port used to lose that the moment it left the field.
    const QString entry = QStringLiteral("%1:%2").arg(host).arg(port);
    m_recentHosts.removeAll(entry);
    m_recentHosts.prepend(entry);
    while (m_recentHosts.size() > kMaxRecentHosts)
        m_recentHosts.removeLast();
    emit connectionInfoChanged();
}

void VitaDevice::selectRecentHost(const QString &entry)
{
    const int colon = entry.lastIndexOf(QLatin1Char(':'));
    if (colon <= 0)
        return;
    bool ok = false;
    const int port = entry.mid(colon + 1).toInt(&ok);
    if (!ok)
        return;
    setHost(entry.left(colon));
    setPort(port);
}

void VitaDevice::clearRecentHosts()
{
    if (m_recentHosts.isEmpty())
        return;
    m_recentHosts.clear();
    emit connectionInfoChanged();
}

} // namespace vsp
