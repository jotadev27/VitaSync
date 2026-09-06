#include "UsbTransport.h"

#include "PathUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#if defined(Q_OS_WIN)
#include <io.h>
#include <qt_windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vsp {

namespace {
constexpr qint64 kCopyChunk = 1 * 1024 * 1024;

// A USB Mass Storage volume is exactly the case QFile::flush() does not
// cover: flush() only pushes QFile's own buffer out to the OS, and the OS is
// free to hold the result in the page cache indefinitely -- normally
// harmless, but this app's whole reason to call something "done" is that the
// Vita can act on it next, and a page still dirty in the host's cache when
// the cable comes out (or VitaShell remounts the card) never reached the
// card at all. `close()` does not imply this either on any Qt backend. This
// is the fix for the confirmed-on-hardware "UI says done, nothing landed"
// bug; see docs/usb.md.
#if defined(Q_OS_WIN)
bool syncFile(QFile &file)
{
    if (!file.flush())
        return false;
    HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(file.handle()));
    return handle != INVALID_HANDLE_VALUE && FlushFileBuffers(handle);
}

// A newly created file or directory is a change to its *parent* directory's
// entries, which is a separate piece of metadata from the file's own bytes
// and is not synced by syncFile() above. exFAT/FAT drivers -- what a Vita
// card almost always is -- commonly do not journal metadata the way a
// modern desktop filesystem does, so skipping this can lose "the file
// exists at all" even when its contents were safely flushed.
bool syncDirectory(const QString &path)
{
    const std::wstring wide = QDir::toNativeSeparators(path).toStdWString();
    HANDLE handle = CreateFileW(wide.c_str(), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const bool ok = FlushFileBuffers(handle);
    CloseHandle(handle);
    return ok;
}
#else
bool syncFile(QFile &file)
{
    if (!file.flush())
        return false;
    return ::fsync(file.handle()) == 0;
}

bool syncDirectory(const QString &path)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY);
    if (fd < 0)
        return false;
    const bool ok = ::fsync(fd) == 0;
    ::close(fd);
    return ok;
}
#endif
}

UsbTransport::UsbTransport(QObject *parent)
    : RemoteTransport(parent)
    , m_watcher(new QFutureWatcher<Outcome>(this))
    , m_mountCheckWatcher(new QFutureWatcher<bool>(this))
{
    connect(m_watcher, &QFutureWatcher<Outcome>::finished, this, &UsbTransport::onWatcherFinished);
    connect(m_watcher, &QFutureWatcher<Outcome>::progressValueChanged, this, [this](int value) {
        if (!m_busy || (m_current.op != Op::Upload && m_current.op != Op::Download))
            return;
        const qint64 done = m_currentTotal > 0 ? (qint64(m_currentTotal) * value) / 1000 : 0;
        emit transferProgress(m_current.id, done, m_currentTotal);
    });
    connect(m_mountCheckWatcher, &QFutureWatcher<bool>::finished,
            this, &UsbTransport::onMountCheckFinished);

    m_mountWatch = new QTimer(this);
    m_mountWatch->setInterval(3000);
    connect(m_mountWatch, &QTimer::timeout, this, &UsbTransport::checkStillMounted);
}

UsbTransport::~UsbTransport()
{
    if (m_watcher->isRunning())
        m_watcher->waitForFinished();
    if (m_mountCheckWatcher->isRunning())
        m_mountCheckWatcher->waitForFinished();
}

// --- detection --------------------------------------------------------------

bool UsbTransport::looksLikeVitaVolume(const QString &rootPath)
{
    if (rootPath.isEmpty())
        return false;
    const QDir dir(rootPath);
    if (!dir.exists())
        return false;

    // id.dat is written by the Vita's own OS to every memory card. Pairing it
    // with VitaShell's own install folder (present on essentially every
    // jailbroken card, since VitaShell is what exposes USB mode in the first
    // place) keeps this from matching an unrelated USB drive that happens to
    // carry a file of the same name. See docs/usb.md.
    if (!QFileInfo(dir.filePath(QStringLiteral("id.dat"))).isFile())
        return false;
    return QFileInfo(dir.filePath(QStringLiteral("VitaShell"))).isDir()
        || QFileInfo(dir.filePath(QStringLiteral("app/VITASHELL"))).isDir();
}

QVector<UsbTransport::Candidate> UsbTransport::detectCandidates(const QStringList &rootsToCheck)
{
    QVector<Candidate> found;
    for (const QString &root : rootsToCheck) {
        if (!looksLikeVitaVolume(root))
            continue;

        const QStorageInfo info(root);
        Candidate candidate;
        candidate.rootPath = QDir(root).absolutePath();
        candidate.label = info.isValid() && !info.name().isEmpty()
                              ? info.name()
                              : QFileInfo(root).fileName();
        candidate.bytesTotal = info.isValid() ? info.bytesTotal() : -1;
        candidate.bytesFree = info.isValid() ? info.bytesAvailable() : -1;
        found.append(candidate);
    }
    return found;
}

QVector<UsbTransport::Candidate> UsbTransport::detectCandidates()
{
    QStringList roots;
    const QList<QStorageInfo> volumes = QStorageInfo::mountedVolumes();
    roots.reserve(volumes.size());
    for (const QStorageInfo &volume : volumes)
        roots << volume.rootPath();
    return detectCandidates(roots);
}

// --- connection ---------------------------------------------------------

bool UsbTransport::attachToMount(const QString &rootPath)
{
    detachFromMount();

    const QFileInfo info(rootPath);
    if (!info.exists() || !info.isDir() || !info.isReadable()) {
        emit errorOccurred(QStringLiteral("Cannot read %1").arg(rootPath));
        return false;
    }

    m_rootPath = QDir(rootPath).absolutePath();
    m_verified = looksLikeVitaVolume(m_rootPath);
    m_state = State::Ready;
    m_mountWatch->start();

    emit logLine(QStringLiteral("> usb: attached %1 (%2)")
                     .arg(m_rootPath, m_verified ? QStringLiteral("verified")
                                                  : QStringLiteral("unverified")));
    emit stateChanged(State::Ready);
    emit connected(m_verified ? QStringLiteral("PS Vita (USB)")
                              : QStringLiteral("Unverified USB volume"));
    return true;
}

void UsbTransport::detachFromMount()
{
    // A voluntary detach, like FtpClient::disconnectFromDevice(): the caller
    // already knows, so only handleMountLoss() (an involuntary one) raises
    // connectionLost()/disconnected().
    abortAll();
    m_mountWatch->stop();
    m_rootPath.clear();
    m_verified = false;
    setState(State::Disconnected);
}

void UsbTransport::checkStillMounted()
{
    if (m_rootPath.isEmpty() || m_state == State::Disconnected)
        return;
    checkMountAsync();
}

void UsbTransport::checkMountAsync(std::optional<Outcome> forRequest)
{
    if (m_mountCheckInFlight) {
        // The periodic timer and a just-failed request both wanting to check
        // at once: whichever request is waiting keeps waiting for the check
        // already running rather than starting a second one, so stash it.
        if (forRequest)
            m_pendingOutcome = std::move(forRequest);
        return;
    }

    m_mountCheckInFlight = true;
    if (forRequest)
        m_pendingOutcome = std::move(forRequest);

    const QString root = m_rootPath;
    m_mountCheckWatcher->setFuture(QtConcurrent::run([root] {
        QStorageInfo info(root);
        info.refresh();
        return info.isValid() && info.isReady();
    }));
}

void UsbTransport::onMountCheckFinished()
{
    m_mountCheckInFlight = false;
    const bool stillMounted = m_mountCheckWatcher->result();

    if (!m_pendingOutcome.has_value()) {
        // Just the periodic health poll; nothing else to resume.
        if (!stillMounted)
            handleMountLoss(QStringLiteral("The USB volume is no longer mounted"));
        return;
    }

    const Outcome outcome = *m_pendingOutcome;
    m_pendingOutcome.reset();

    if (!m_busy) {
        // Whatever this check was gating (a cancel, or the mount already
        // being declared lost some other way) already resolved it while the
        // check was running; there is nothing left to complete.
        return;
    }

    if (!stillMounted) {
        handleMountLoss(QStringLiteral("The USB volume is no longer mounted"));
        return;
    }
    completeRequest(outcome);
}

void UsbTransport::handleMountLoss(const QString &message)
{
    if (m_state == State::Disconnected)
        return;

    const bool hadRequest = m_busy;
    const int requestId = m_current.id;

    m_mountWatch->stop();
    m_queue.clear();
    m_expectingFinish = false;
    m_pendingOutcome.reset();
    m_busy = false;
    m_current = {};
    m_verified = false;
    m_state = State::Disconnected;

    emit logLine(QStringLiteral("! usb: %1").arg(message));

    if (hadRequest) {
        // Cause before effect, so the owner of the request can tell "the
        // device went away" from "the device refused this" -- exactly the
        // ordering FtpClient::handleConnectionLoss uses.
        emit connectionLost();
        emit commandFinished(requestId, false, message);
    } else {
        emit errorOccurred(message);
    }

    emit stateChanged(State::Disconnected);
    emit disconnected();
}

// --- request entry points ------------------------------------------------

int UsbTransport::list(const QString &remoteDir)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::List;
    request.primaryPath = path::normalizeRemote(remoteDir);
    enqueue(request);
    return request.id;
}

int UsbTransport::upload(const QString &localPath, const QString &remotePath)
{
    if (!isConnected())
        return 0;
    if (!QFileInfo::exists(localPath)) {
        emit errorOccurred(QStringLiteral("Local file no longer exists"));
        return 0;
    }
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Upload;
    request.localPath = localPath;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int UsbTransport::download(const QString &remotePath, const QString &localPath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Download;
    request.primaryPath = path::normalizeRemote(remotePath);
    request.localPath = localPath;
    enqueue(request);
    return request.id;
}

int UsbTransport::makeDirectory(const QString &remotePath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Mkdir;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int UsbTransport::removeFile(const QString &remotePath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::RemoveFile;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int UsbTransport::removeDirectory(const QString &remotePath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::DeleteDir;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int UsbTransport::rename(const QString &fromPath, const QString &toPath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Rename;
    request.primaryPath = path::normalizeRemote(fromPath);
    request.secondaryPath = path::normalizeRemote(toPath);
    enqueue(request);
    return request.id;
}

int UsbTransport::requestSize(const QString &remotePath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Size;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int UsbTransport::ping()
{
    // There is no session to keep alive: a mounted filesystem does not time
    // out the way an idle FTP control channel does. Kept only so callers
    // written against the interface have something to call; answered
    // asynchronously like every other request, never inline.
    if (!isConnected())
        return 0;
    const int id = m_nextRequestId++;
    QMetaObject::invokeMethod(
        this, [this, id] { emit commandFinished(id, true, QStringLiteral("ok")); },
        Qt::QueuedConnection);
    return id;
}

void UsbTransport::abortAll()
{
    const bool hadWork = m_busy || !m_queue.isEmpty();
    m_queue.clear();

    if (m_busy) {
        const int id = m_current.id;
        m_expectingFinish = false;
        // A mount check may be gating this very request's completion; the
        // m_busy guard in onMountCheckFinished() would already stop it from
        // double-reporting, but there is no reason to let it complete a
        // request that has just been answered here instead.
        m_pendingOutcome.reset();
        m_watcher->future().cancel();
        m_busy = false;
        m_current = {};
        emit commandFinished(id, false, QStringLiteral("Cancelled"));
    }

    if (hadWork && isConnected())
        setState(State::Ready);
}

// --- queue -----------------------------------------------------------------

void UsbTransport::enqueue(const Request &request)
{
    m_queue.enqueue(request);
    pumpQueue();
}

void UsbTransport::pumpQueue()
{
    if (m_busy || m_queue.isEmpty() || !isConnected())
        return;

    m_current = m_queue.dequeue();
    m_busy = true;
    setState(State::Busy);
    beginRequest(m_current);
}

void UsbTransport::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

void UsbTransport::beginRequest(const Request &request)
{
    QString error;
    m_currentTotal = 0;

    switch (request.op) {
    case Op::List: {
        const QString localDir = mapToLocal(request.primaryPath, &error);
        if (localDir.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(QtConcurrent::run(&UsbTransport::runList, localDir, request.primaryPath));
        break;
    }

    case Op::Upload: {
        const QString targetLocal = mapToLocal(request.primaryPath, &error);
        if (targetLocal.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_currentTotal = QFileInfo(request.localPath).size();
        m_expectingFinish = true;
        m_watcher->setFuture(
            QtConcurrent::run(&UsbTransport::runCopyFile, request.localPath, targetLocal));
        break;
    }

    case Op::Download: {
        const QString sourceLocal = mapToLocal(request.primaryPath, &error);
        if (sourceLocal.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_currentTotal = QFileInfo(sourceLocal).size();
        m_expectingFinish = true;
        m_watcher->setFuture(
            QtConcurrent::run(&UsbTransport::runCopyFile, sourceLocal, request.localPath));
        break;
    }

    case Op::Mkdir: {
        const QString localDir = mapToLocal(request.primaryPath, &error);
        if (localDir.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(QtConcurrent::run(&UsbTransport::runMkdir, localDir));
        break;
    }

    case Op::RemoveFile: {
        const QString localPath = mapToLocal(request.primaryPath, &error);
        if (localPath.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(QtConcurrent::run(&UsbTransport::runRemoveFile, localPath));
        break;
    }

    case Op::DeleteDir: {
        const QString localPath = mapToLocal(request.primaryPath, &error);
        if (localPath.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(QtConcurrent::run(&UsbTransport::runRemoveDirectory, localPath));
        break;
    }

    case Op::Rename: {
        const QString fromLocal = mapToLocal(request.primaryPath, &error);
        if (fromLocal.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        const QString toLocal = mapToLocal(request.secondaryPath, &error);
        if (toLocal.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(QtConcurrent::run(&UsbTransport::runRename, fromLocal, toLocal));
        break;
    }

    case Op::Size: {
        const QString localPath = mapToLocal(request.primaryPath, &error);
        if (localPath.isEmpty() && !error.isEmpty()) {
            rejectRequest(error);
            return;
        }
        m_expectingFinish = true;
        m_watcher->setFuture(
            QtConcurrent::run(&UsbTransport::runSize, localPath, request.primaryPath));
        break;
    }

    case Op::None:
        rejectRequest(QStringLiteral("Empty request"));
        break;
    }
}

void UsbTransport::onWatcherFinished()
{
    if (!m_expectingFinish)
        return;
    m_expectingFinish = false;

    if (m_watcher->future().resultCount() == 0)
        return;
    finishRequest(m_watcher->result());
}

void UsbTransport::finishRequest(const Outcome &outcome)
{
    // A failed request might mean the device went away, which is worth
    // checking -- but on real hardware a mounted USB volume can be slow to
    // stat, so that check must happen off the GUI thread rather than block
    // it right here. m_current/m_busy stay exactly as they are (single
    // request in flight) until checkMountAsync()'s continuation runs.
    if (!outcome.success && !m_rootPath.isEmpty()) {
        checkMountAsync(outcome);
        return;
    }
    completeRequest(outcome);
}

void UsbTransport::completeRequest(const Outcome &outcome)
{
    const int id = m_current.id;
    const Op op = m_current.op;
    const QString primaryPath = m_current.primaryPath;

    m_busy = false;
    m_current = {};

    if (isConnected())
        setState(State::Ready);

    if (op == Op::List)
        emit listingReady(id, primaryPath, outcome.entries);
    else if (op == Op::Size)
        emit sizeReady(id, primaryPath, outcome.size);

    emit commandFinished(id, outcome.success, outcome.message);
    pumpQueue();
}

void UsbTransport::rejectRequest(const QString &message)
{
    // The request never reached a worker at all -- primaryPath simply did
    // not map onto ux0:, or the request was empty -- so there is nothing
    // that could mean the device went away. No mount check: this is exactly
    // the path Root takes over USB (only ux0: is reachable at all -- see
    // docs/usb.md), and it must never cost a stat against a real, possibly
    // slow, device it never even touched.
    //
    // Answered on the next event-loop turn, never inline. Every caller in
    // this codebase does `requestId = list(...)` and only afterwards records
    // requestId to match the reply against -- FtpClient's requests are
    // always genuinely asynchronous (a socket round trip), so that ordering
    // never mattered before. This is the one path where UsbTransport could
    // finish before the caller's own assignment did, which made the reply
    // arrive for an id nobody was listening for yet -- silently dropped, so
    // the UI never found out the request had failed at all and looked stuck
    // rather than merely wrong.
    const int id = m_current.id;
    QMetaObject::invokeMethod(this, [this, id, message] {
        if (!m_busy || m_current.id != id)
            return; // resolved some other way already (e.g. abortAll)
        m_busy = false;
        m_current = {};
        if (isConnected())
            setState(State::Ready);
        emit commandFinished(id, false, message);
        pumpQueue();
    }, Qt::QueuedConnection);
}

// --- path mapping ------------------------------------------------------

QString UsbTransport::mapToLocal(const QString &remotePath, QString *error) const
{
    const QString normalized = path::normalizeRemote(remotePath);
    if (path::mountOf(normalized) != QLatin1String("ux0:")) {
        if (error) {
            *error = QStringLiteral("USB mode only reaches ux0: (asked for %1)").arg(normalized);
        }
        return {};
    }

    QString remainder = normalized.mid(5); // strip the leading "/ux0:"
    while (remainder.startsWith(QLatin1Char('/')))
        remainder.remove(0, 1);

    if (error)
        error->clear();
    return remainder.isEmpty() ? m_rootPath : QDir(m_rootPath).filePath(remainder);
}

// --- workers (run off the GUI thread) --------------------------------------

UsbTransport::Outcome UsbTransport::runList(QString localDir, QString remoteDir)
{
    Outcome outcome;
    const QDir dir(localDir);
    if (!dir.exists()) {
        outcome.message = QStringLiteral("Cannot open that folder");
        return outcome;
    }

    const QFileInfoList infos = dir.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);

    QVector<RemoteEntry> entries;
    entries.reserve(infos.size());
    for (const QFileInfo &info : infos) {
        if (!path::isSafeComponent(info.fileName()))
            continue;
        RemoteEntry entry;
        entry.name = info.fileName();
        entry.path = path::joinRemote(remoteDir, entry.name);
        entry.isDirectory = info.isDir();
        entry.size = entry.isDirectory ? -1 : info.size();
        entry.modified = info.lastModified();
        entry.permissions = info.isDir() ? QStringLiteral("drwxrwxrwx") : QStringLiteral("-rwxrwxrwx");
        entries.append(entry);
    }

    outcome.success = true;
    outcome.entries = entries;
    return outcome;
}

void UsbTransport::runCopyFile(QPromise<Outcome> &promise, QString fromPath, QString toPath)
{
    Outcome outcome;

    QFile source(fromPath);
    if (!source.open(QIODevice::ReadOnly)) {
        outcome.message = QStringLiteral("Cannot read %1").arg(fromPath);
        promise.addResult(outcome);
        return;
    }

    if (!QDir().mkpath(QFileInfo(toPath).absolutePath())) {
        outcome.message = QStringLiteral("Cannot create %1").arg(QFileInfo(toPath).absolutePath());
        promise.addResult(outcome);
        return;
    }

    QFile target(toPath);
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        outcome.message = QStringLiteral("Cannot write to %1").arg(toPath);
        promise.addResult(outcome);
        return;
    }

    const qint64 total = source.size();
    promise.setProgressRange(0, 1000);
    qint64 done = 0;

    while (!source.atEnd()) {
        if (promise.isCanceled()) {
            outcome.message = QStringLiteral("Cancelled");
            promise.addResult(outcome);
            return;
        }

        const QByteArray chunk = source.read(kCopyChunk);
        if (target.write(chunk) != chunk.size()) {
            outcome.message = QStringLiteral("Ran out of space while writing %1").arg(toPath);
            promise.addResult(outcome);
            return;
        }

        done += chunk.size();
        if (total > 0)
            promise.setProgressValue(static_cast<int>((1000.0 * double(done)) / double(total)));
    }

    // Durable before this says so -- not merely written into QFile's buffer,
    // and not merely handed to the OS, but actually on the card. See the
    // comment on syncFile() above.
    if (!syncFile(target)) {
        outcome.message = QStringLiteral("Could not flush %1 to the device").arg(toPath);
        promise.addResult(outcome);
        return;
    }
    target.close();
    source.close();

    if (!syncDirectory(QFileInfo(toPath).absolutePath())) {
        outcome.message = QStringLiteral("Could not flush the destination folder to the device");
        promise.addResult(outcome);
        return;
    }

    outcome.success = true;
    outcome.message = QStringLiteral("Transfer complete");
    promise.addResult(outcome);
}

UsbTransport::Outcome UsbTransport::runMkdir(QString localDir)
{
    Outcome outcome;
    // Matches real MKD semantics (and MockVitaServer's): asking for a
    // directory that already exists fails. TransferQueue already tolerates
    // that -- see its comment on the second-install case -- so this keeps USB
    // behaviourally identical rather than silently more lenient.
    if (QFileInfo::exists(localDir)) {
        outcome.message = QStringLiteral("Directory already exists");
        return outcome;
    }
    if (!QDir().mkpath(localDir)) {
        outcome.message = QStringLiteral("Cannot create %1").arg(localDir);
        return outcome;
    }
    // Durable before this says so, the same reasoning as runCopyFile(): a
    // folder-format install's directory tree is created via calls to this
    // before any file lands inside, so an unflushed mkdir is just as capable
    // of losing the job's work as an unflushed file write.
    if (!syncDirectory(localDir) || !syncDirectory(QFileInfo(localDir).absolutePath())) {
        outcome.message = QStringLiteral("Created %1 but could not flush it to the device").arg(localDir);
        return outcome;
    }
    outcome.success = true;
    outcome.message = QStringLiteral("Directory created");
    return outcome;
}

UsbTransport::Outcome UsbTransport::runRemoveFile(QString localPath)
{
    Outcome outcome;
    QFile file(localPath);
    if (!file.exists()) {
        outcome.message = QStringLiteral("No such file");
        return outcome;
    }
    outcome.success = file.remove();
    if (!outcome.success)
        outcome.message = QStringLiteral("Cannot delete %1").arg(localPath);
    return outcome;
}

UsbTransport::Outcome UsbTransport::runRemoveDirectory(QString localPath)
{
    Outcome outcome;
    QDir dir(localPath);
    if (!dir.exists()) {
        outcome.message = QStringLiteral("No such folder");
        return outcome;
    }
    // VitaShell's own RMD recursively deletes rather than requiring an empty
    // folder first -- confirmed against MockVitaServer, which reproduces it.
    outcome.success = dir.removeRecursively();
    if (!outcome.success)
        outcome.message = QStringLiteral("Cannot delete %1").arg(localPath);
    return outcome;
}

UsbTransport::Outcome UsbTransport::runRename(QString fromLocal, QString toLocal)
{
    Outcome outcome;
    QDir().mkpath(QFileInfo(toLocal).absolutePath());
    outcome.success = QDir().rename(fromLocal, toLocal);
    if (!outcome.success)
        outcome.message = QStringLiteral("Cannot rename to %1").arg(toLocal);
    return outcome;
}

UsbTransport::Outcome UsbTransport::runSize(QString localPath, QString remotePath)
{
    Q_UNUSED(remotePath)
    Outcome outcome;
    const QFileInfo info(localPath);
    if (!info.exists()) {
        outcome.message = QStringLiteral("No such file");
        return outcome;
    }
    outcome.success = true;
    outcome.size = info.size();
    return outcome;
}

} // namespace vsp
