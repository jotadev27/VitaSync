#include "TransferQueue.h"

#include "ArchiveExtractor.h"
#include "CompanionClient.h"
#include "PathUtils.h"
#include "RemoteTransport.h"
#include "RemoteTreeScanner.h"
#include "VitaPaths.h"
#include "ZipReader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace vsp {

TransferQueue::TransferQueue(QObject *parent)
    : QAbstractListModel(parent)
    , m_hashWatcher(new QFutureWatcher<QString>(this))
{
    connect(m_hashWatcher, &QFutureWatcher<QString>::finished, this, [this] {
        const int row = m_hashingRow;
        m_hashingRow = -1;
        if (row < 0 || row >= m_jobs.size())
            return;

        m_jobs[row].localSha256 = m_hashWatcher->result();
        emit logLine(QStringLiteral("sha256 %1 %2")
                         .arg(m_jobs.at(row).localSha256.left(16),
                              QFileInfo(m_jobs.at(row).localPath).fileName()));
        beginTransfer(row);
    });

    m_extractWatcher = new QFutureWatcher<ExtractionResult>(this);
    connect(m_extractWatcher, &QFutureWatcher<ExtractionResult>::finished, this, [this] {
        const int row = m_extractingRow;
        m_extractingRow = -1;
        if (row < 0 || row >= m_jobs.size())
            return;

        const ExtractionResult extraction = m_extractWatcher->result();
        if (!extraction.ok) {
            completeJob(row, false, extraction.error);
            return;
        }

        emit logLine(QStringLiteral("prepared %1 file(s), %2 from %3")
                         .arg(extraction.files.size())
                         .arg(path::humanSize(extraction.totalBytes),
                              extraction.rootPath));
        if (extraction.skipped > 0) {
            emit logLine(QStringLiteral("! %1 archive entr(y/ies) refused as unsafe")
                             .arg(extraction.skipped));
        }
        buildUploadPlan(row, extraction);
    });
}

TransferQueue::~TransferQueue()
{
    if (m_hashWatcher->isRunning())
        m_hashWatcher->waitForFinished();
    if (m_extractWatcher->isRunning())
        m_extractWatcher->waitForFinished();

    // Anything unpacked for a theme is scratch space; do not leave it behind.
    for (Job &job : m_jobs)
        cleanupTemp(&job);
}

void TransferQueue::attach(RemoteTransport *ftp, CompanionClient *companion)
{
    // Safe to call again with a different transport -- switching Wi-Fi/USB
    // mode reattaches to a new object, and the old connections must not
    // linger and double-fire.
    if (m_ftp)
        disconnect(m_ftp, nullptr, this, nullptr);
    delete m_scanner;
    m_scanner = nullptr;

    m_ftp = ftp;
    m_companion = companion;
    if (!m_ftp)
        return;

    connect(m_ftp, &RemoteTransport::commandFinished, this, &TransferQueue::onCommandFinished);
    connect(m_ftp, &RemoteTransport::transferProgress, this, &TransferQueue::onTransferProgress);
    connect(m_ftp, &RemoteTransport::sizeReady, this, &TransferQueue::onSizeReady);
    connect(m_ftp, &RemoteTransport::connectionLost, this, [this] { m_connectionLost = true; });
    connect(m_ftp, &RemoteTransport::disconnected, this, [this] {
        // The next session may be a different console; do not assume the
        // folders created on the last one are there.
        m_createdDirectories.clear();
    });

    // The scanner issues its own LIST requests through the same client. They
    // carry request ids this queue does not own, so they are ignored here.
    m_scanner = new RemoteTreeScanner(m_ftp, this);
    connect(m_scanner, &RemoteTreeScanner::scanProgress, this,
            [this](int directories, int files) {
                if (m_scanningRow < 0)
                    return;
                m_jobs[m_scanningRow].message =
                    QStringLiteral("%1 folder(s), %2 file(s)").arg(directories).arg(files);
                touch(m_scanningRow, { MessageRole });
            });
    connect(m_scanner, &RemoteTreeScanner::finished, this,
            [this](bool ok, const QString &message) {
                const int row = m_scanningRow;
                m_scanningRow = -1;
                if (row < 0 || row >= m_jobs.size())
                    return;
                if (!ok) {
                    completeJob(row, false, message);
                    return;
                }
                buildDownloadPlan(row);
            });
}

// --- model ----------------------------------------------------------------

int TransferQueue::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_jobs.size());
}

QHash<int, QByteArray> TransferQueue::roleNames() const
{
    return {
        { JobIdRole,      "jobId" },
        { KindRole,       "kind" },
        { KindLabelRole,  "kindLabel" },
        { TitleRole,      "title" },
        { SubtitleRole,   "subtitle" },
        // Deliberately not "state": every QML Item already has a `state`
        // property, and a role by that name is silently shadowed inside a
        // delegate -- the binding reads the Item's empty string instead.
        { StateRole,      "jobState" },
        { StateLabelRole, "stateLabel" },
        { ProgressRole,   "progress" },
        { DoneTextRole,   "doneText" },
        { TotalTextRole,  "totalText" },
        { RateTextRole,   "rateText" },
        { MessageRole,    "message" },
        { IconSourceRole, "iconSource" },
        { RemotePathRole, "remotePath" },
        { LocalPathRole,  "localPath" },
        { IsFinishedRole, "isFinished" },
        { IsFailedRole,   "isFailed" },
        { VerifiedRole,   "verified" },
        { FileProgressRole, "fileProgress" },
        { NextStepRole,   "nextStep" }
    };
}

QString TransferQueue::stateLabel(const Job &job)
{
    switch (job.state) {
    case JobState::Pending:        return QStringLiteral("Queued");
    case JobState::Checksumming:   return QStringLiteral("Checksum");
    case JobState::Unpacking:      return QStringLiteral("Unpacking");
    case JobState::Scanning:       return QStringLiteral("Scanning");
    case JobState::Transferring:
        if (job.phase == Phase::MakeDirectories)
            return QStringLiteral("Preparing");
        return (job.kind == Kind::Download || job.kind == Kind::DownloadFolder)
                   ? QStringLiteral("Downloading")
                   : QStringLiteral("Uploading");
    case JobState::Verifying:      return QStringLiteral("Verifying");
    case JobState::AwaitingDevice: return QStringLiteral("Confirm on Vita");
    case JobState::Complete:       return QStringLiteral("Done");
    case JobState::Failed:         return QStringLiteral("Failed");
    case JobState::Cancelled:      return QStringLiteral("Cancelled");
    }
    return {};
}

QVariant TransferQueue::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_jobs.size())
        return {};

    const Job &job = m_jobs.at(index.row());
    switch (role) {
    case JobIdRole:      return job.id;
    case KindRole:       return static_cast<int>(job.kind);
    case KindLabelRole:  return job.kindLabel;
    case TitleRole:      return job.title;
    case SubtitleRole:   return job.subtitle;
    case StateRole:      return static_cast<int>(job.state);
    case StateLabelRole: return stateLabel(job);
    case ProgressRole:   return job.total > 0
                                    ? qBound(0.0, static_cast<double>(job.done) / static_cast<double>(job.total), 1.0)
                                    : (job.state == JobState::Complete ? 1.0 : 0.0);
    case DoneTextRole:   return path::humanSize(job.done);
    case TotalTextRole:  return path::humanSize(job.total);
    case RateTextRole:   return job.state == JobState::Transferring
                                    ? path::humanRate(job.rate) : QString();
    case MessageRole:    return job.message;
    case IconSourceRole: return job.iconSource;
    case RemotePathRole: return job.remotePath;
    case LocalPathRole:  return job.localPath;
    case IsFinishedRole: return job.state == JobState::Complete
                                 || job.state == JobState::Failed
                                 || job.state == JobState::Cancelled;
    case IsFailedRole:   return job.state == JobState::Failed;
    case VerifiedRole:   return job.verified;
    case FileProgressRole:
        if (job.files.isEmpty())
            return QString();
        return QStringLiteral("%1 / %2 files")
            .arg(qMin(job.fileIndex + 1, static_cast<int>(job.files.size())))
            .arg(job.files.size());
    case NextStepRole:   return job.nextStep;
    default:             return {};
    }
}

bool TransferQueue::isTreeJob(const Job &job)
{
    return job.kind == Kind::InstallTheme || job.kind == Kind::DownloadFolder;
}

void TransferQueue::touch(int row, const QList<int> &roles)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, roles);
    emit summaryChanged();
}

// --- summary --------------------------------------------------------------

int TransferQueue::activeCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        switch (job.state) {
        case JobState::Transferring:
        case JobState::Checksumming:
        case JobState::Unpacking:
        case JobState::Scanning:
        case JobState::Verifying:
            ++count;
            break;
        default:
            break;
        }
    }
    return count;
}

int TransferQueue::pendingCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (job.state == JobState::Pending)
            ++count;
    }
    return count;
}

int TransferQueue::failedCount() const
{
    int count = 0;
    for (const Job &job : m_jobs) {
        if (job.state == JobState::Failed)
            ++count;
    }
    return count;
}

bool TransferQueue::isBusy() const
{
    return m_runningRow >= 0 || pendingCount() > 0;
}

qreal TransferQueue::overallProgress() const
{
    qint64 done = 0;
    qint64 total = 0;
    for (const Job &job : m_jobs) {
        if (job.state == JobState::Complete) {
            done += job.total;
            total += job.total;
        } else if (job.state == JobState::Pending || job.state == JobState::Transferring
                   || job.state == JobState::Checksumming || job.state == JobState::Unpacking
                   || job.state == JobState::Scanning || job.state == JobState::Verifying) {
            done += job.done;
            total += job.total;
        }
    }
    if (total <= 0)
        return 0.0;
    return qBound(0.0, static_cast<double>(done) / static_cast<double>(total), 1.0);
}

// --- enqueueing -----------------------------------------------------------

QString TransferQueue::cacheIconFor(const PackageInfo &info) const
{
    if (info.iconPng.isEmpty())
        return info.coverPath.isEmpty() ? QString() : QUrl::fromLocalFile(info.coverPath).toString();

    const QString cacheRoot = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir dir(cacheRoot + QStringLiteral("/icons"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral(".")))
        return {};

    const QString stem = info.titleId.isEmpty()
                             ? QFileInfo(info.fileName).completeBaseName()
                             : info.titleId;
    const QString target = dir.filePath(path::sanitizeFileName(stem) + QStringLiteral(".png"));

    QFile file(target);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    file.write(info.iconPng);
    file.close();

    return QUrl::fromLocalFile(target).toString();
}

void TransferQueue::appendJob(Job job)
{
    job.id = m_nextJobId++;
    beginInsertRows({}, static_cast<int>(m_jobs.size()), static_cast<int>(m_jobs.size()));
    m_jobs.append(std::move(job));
    endInsertRows();
    emit summaryChanged();
    advance();
}

int TransferQueue::enqueueUpload(const PackageInfo &info, const QString &remoteDir, bool install)
{
    Job job;
    job.kind = install ? Kind::Install : Kind::Upload;
    job.localPath = info.localPath;
    job.remoteDir = path::normalizeRemote(remoteDir);
    job.remotePath = path::joinRemote(job.remoteDir, info.fileName);
    job.title = info.displayName();
    job.subtitle = info.titleId.isEmpty()
                       ? info.fileName
                       : QStringLiteral("%1  ·  %2").arg(info.titleId, info.fileName);
    job.kindLabel = packageKindLabel(info.kind);
    job.titleId = info.titleId;
    job.total = info.fileSize;
    job.wantsInstall = install;
    job.iconSource = cacheIconFor(info);

    const int id = m_nextJobId;
    appendJob(std::move(job));
    return id;
}

int TransferQueue::enqueueRawUpload(const QString &localPath, const QString &remoteDir)
{
    const QFileInfo fileInfo(localPath);
    if (!fileInfo.exists())
        return 0;

    Job job;
    job.kind = Kind::Upload;
    job.localPath = localPath;
    job.remoteDir = path::normalizeRemote(remoteDir);
    job.remotePath = path::joinRemote(job.remoteDir, fileInfo.fileName());
    job.title = fileInfo.fileName();
    job.subtitle = job.remoteDir;
    job.kindLabel = packageKindLabel(PackageInspector::kindForExtension(fileInfo.fileName()));
    job.total = fileInfo.size();

    const int id = m_nextJobId;
    appendJob(std::move(job));
    return id;
}

int TransferQueue::enqueueFolderInstall(const PackageInfo &info, const QString &remoteParentDir)
{
    if (!info.isDirectoryPayload() || !info.valid)
        return 0;

    // The folder name on the device: a game must land under its Title ID for
    // the console to recognise it, a theme under the name it ships with.
    QString folder = info.themeFolderName;
    if (info.kind == PackageKind::FolderGame)
        folder = info.titleId;
    if (folder.isEmpty())
        folder = info.fileName;
    folder = path::sanitizeFileName(folder);

    Job job;
    job.kind = Kind::InstallTheme;
    job.localPath = info.localPath;
    job.remoteDir = path::normalizeRemote(remoteParentDir);
    job.remotePath = path::joinRemote(job.remoteDir, folder);
    job.title = info.displayName();
    job.kindLabel = packageKindLabel(info.kind);
    job.archivePrefix = info.archivePrefix;
    job.iconSource = cacheIconFor(info);
    job.wantsInstall = true;
    // Until the archive is opened the unpacked size is the best estimate we
    // have; it is replaced with the real total once extraction finishes.
    job.total = info.unpackedSize > 0 ? info.unpackedSize : info.fileSize;

    job.subtitle = info.provider.isEmpty()
                       ? job.remotePath
                       : QStringLiteral("%1  ·  %2").arg(info.provider, job.remotePath);
    job.sourceIsDirectory = info.localIsDirectory;

    // The step after the transfer differs by flavour, and neither is something
    // this app can do for the user.
    switch (info.kind) {
    case PackageKind::SystemTheme:
        job.nextStep = QStringLiteral("Apply it in Custom Themes Manager");
        break;
    case PackageKind::ShellTheme:
        job.nextStep = QStringLiteral("Pick it in VitaShell: START, then Restart VitaShell");
        break;
    case PackageKind::FolderGame:
        // Nothing installs an unpacked game: the folder is already in its
        // installed shape, and the console just has to be told to look again.
        job.nextStep = QStringLiteral("Refresh LiveArea in VitaShell to see it");
        break;
    default:
        break;
    }

    const int id = m_nextJobId;
    appendJob(std::move(job));
    return id;
}

int TransferQueue::enqueueFolderDownload(const QString &remotePath, const QString &localDir,
                                         const QString &displayName)
{
    const QString name = path::baseNameOfRemote(remotePath);
    const QString root = path::safeLocalTarget(localDir, name);
    if (root.isEmpty())
        return 0;

    Job job;
    job.kind = Kind::DownloadFolder;
    job.remotePath = path::normalizeRemote(remotePath);
    job.remoteDir = path::parentOfRemote(job.remotePath);
    job.localRoot = root;
    job.localPath = root;
    job.title = displayName.isEmpty() ? name : displayName;
    job.subtitle = job.remotePath;
    job.kindLabel = QStringLiteral("Folder");

    const int id = m_nextJobId;
    appendJob(std::move(job));
    return id;
}

int TransferQueue::enqueueDownload(const QString &remotePath, qint64 remoteSize,
                                   const QString &localDir, const QString &displayName)
{
    // The remote name is untrusted input; resolve it to a path we know stays
    // inside the folder the user picked before we ever open it for writing.
    const QString name = path::baseNameOfRemote(remotePath);
    const QString target = path::safeLocalTarget(localDir, name);
    if (target.isEmpty())
        return 0;

    Job job;
    job.kind = Kind::Download;
    job.localPath = target;
    job.remotePath = path::normalizeRemote(remotePath);
    job.remoteDir = path::parentOfRemote(job.remotePath);
    job.title = displayName.isEmpty() ? name : displayName;
    job.subtitle = job.remoteDir;
    job.kindLabel = packageKindLabel(PackageInspector::kindForExtension(name));
    job.total = qMax<qint64>(remoteSize, 0);

    const int id = m_nextJobId;
    appendJob(std::move(job));
    return id;
}

// --- scheduling -----------------------------------------------------------

int TransferQueue::indexOfJob(int jobId) const
{
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).id == jobId)
            return i;
    }
    return -1;
}

int TransferQueue::indexOfRequest(int requestId) const
{
    if (requestId <= 0)
        return -1;
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs.at(i).ftpRequestId == requestId)
            return i;
    }
    return -1;
}

void TransferQueue::start()
{
    advance();
}

void TransferQueue::advance()
{
    if (m_runningRow >= 0 || m_hashingRow >= 0 || m_extractingRow >= 0 || m_scanningRow >= 0)
        return;
    if (!m_ftp || !m_ftp->isConnected())
        return;

    for (int row = 0; row < m_jobs.size(); ++row) {
        if (m_jobs.at(row).state == JobState::Pending) {
            beginJob(row);
            return;
        }
    }
}

void TransferQueue::beginJob(int row)
{
    Job &job = m_jobs[row];
    job.clock.start();
    job.lastSampleBytes = 0;
    job.lastSampleMs = 0;
    job.done = 0;
    job.completedBytes = 0;
    job.fileIndex = 0;
    job.directoryIndex = 0;

    if (job.kind == Kind::InstallTheme) {
        m_runningRow = row;
        // The plan is only known once the archive is on disk, so a theme opens
        // by unpacking rather than by talking to the device.
        if (job.files.isEmpty())
            startFolderPreparation(row);
        else
            runTreePhase(row);
        return;
    }

    if (job.kind == Kind::DownloadFolder) {
        m_runningRow = row;
        if (job.files.isEmpty() && job.directories.isEmpty())
            startFolderScan(row);
        else
            runTreePhase(row);
        return;
    }

    if (job.kind == Kind::Download) {
        m_runningRow = row;
        beginTransfer(row);
        return;
    }

    // Uploads that will be installed get hashed first: the checksum is what
    // makes "the transfer landed intact" a statement rather than a hope.
    if (job.wantsInstall && job.localSha256.isEmpty()) {
        job.state = JobState::Checksumming;
        m_hashingRow = row;
        m_runningRow = row;
        touch(row);
        const QString localPath = job.localPath;
        m_hashWatcher->setFuture(QtConcurrent::run([localPath] {
            return PackageInspector::sha256OfFile(localPath);
        }));
        return;
    }

    m_runningRow = row;
    beginTransfer(row);
}

void TransferQueue::beginTransfer(int row)
{
    if (row < 0 || row >= m_jobs.size() || !m_ftp)
        return;

    Job &job = m_jobs[row];
    m_runningRow = row;
    job.state = JobState::Transferring;
    job.clock.restart();
    touch(row);

    if (job.kind == Kind::Download) {
        job.ftpRequestId = m_ftp->download(job.remotePath, job.localPath);
    } else {
        // Make sure the destination exists, but only once per folder per
        // session. Five photos into one folder used to mean five MKDs; every
        // command saved is one less thing for a busy device to trip over.
        if (!m_createdDirectories.contains(job.remoteDir)) {
            m_createdDirectories.insert(job.remoteDir);
            m_mkdirRequestId = m_ftp->makeDirectory(job.remoteDir);
        }
        job.ftpRequestId = m_ftp->upload(job.localPath, job.remotePath);
    }

    if (job.ftpRequestId == 0)
        completeJob(row, false, QStringLiteral("Not connected"));
}

void TransferQueue::startFolderPreparation(int row)
{
    Job &job = m_jobs[row];
    job.state = JobState::Unpacking;
    touch(row);

    // An unpacked game is already on disk: enumerate it rather than making a
    // second copy of a folder that can run to gigabytes.
    if (job.sourceIsDirectory) {
        job.message = QStringLiteral("Reading folder");
        const QString source = job.localPath;
        m_extractingRow = row;
        m_extractWatcher->setFuture(QtConcurrent::run([source] {
            ExtractionLimits limits;
            // A game is not a theme; it is allowed to be large.
            limits.maxTotalBytes = 32LL * 1024 * 1024 * 1024;
            limits.maxFileBytes = 8LL * 1024 * 1024 * 1024;
            limits.maxFiles = 20000;
            limits.maxDepth = 12;
            return ArchiveExtractor::collectDirectory(source, limits);
        }));
        return;
    }

    job.message = QStringLiteral("Reading archive");

    // Scratch space under the system temp dir, one folder per job so two
    // themes with the same internal name cannot collide.
    const QString tempRoot =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/vitasync/theme-%1-%2")
              .arg(job.id)
              .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    job.tempRoot = tempRoot;

    const QString archivePath = job.localPath;
    const QString prefix = job.archivePrefix;
    m_extractingRow = row;
    m_extractWatcher->setFuture(QtConcurrent::run([archivePath, prefix, tempRoot] {
        ZipReader archive;
        ExtractionResult result;
        if (!archive.open(archivePath)) {
            result.error = archive.errorString();
            return result;
        }
        return ArchiveExtractor::extractSubtree(&archive, prefix, tempRoot);
    }));
}

void TransferQueue::buildUploadPlan(int row, const ExtractionResult &extraction)
{
    Job &job = m_jobs[row];

    QSet<QString> directorySet;
    job.files.clear();
    job.total = 0;

    for (const ExtractedFile &file : extraction.files) {
        FilePair pair;
        pair.localPath = file.localPath;
        pair.size = file.size;

        // Rebuild the remote path component by component so the sanitiser runs
        // on every part, not just the leaf.
        QString remote = job.remotePath;
        const QStringList parts = file.relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (int i = 0; i < parts.size(); ++i) {
            remote = path::joinRemote(remote, parts.at(i));
            if (i + 1 < parts.size())
                directorySet.insert(remote);
        }
        pair.remotePath = remote;

        job.files.append(pair);
        job.total += pair.size;
    }

    if (job.files.isEmpty()) {
        completeJob(row, false, QStringLiteral("The theme folder was empty"));
        return;
    }

    // Every ancestor of the theme folder needs creating too, shallowest first,
    // because the Vita's MKD does not create parents.
    QStringList directories;
    QString walk = job.remotePath;
    QStringList ancestors;
    while (!walk.isEmpty() && walk != QLatin1String("/") && !path::isMountRoot(walk)) {
        ancestors.prepend(walk);
        walk = path::parentOfRemote(walk);
    }
    directories = ancestors;

    QStringList nested(directorySet.cbegin(), directorySet.cend());
    std::sort(nested.begin(), nested.end(), [](const QString &a, const QString &b) {
        return a.count(QLatin1Char('/')) < b.count(QLatin1Char('/'));
    });
    directories.append(nested);

    job.directories = directories;
    job.phase = Phase::MakeDirectories;
    job.directoryIndex = 0;
    job.state = JobState::Transferring;
    job.message.clear();
    touch(row);
    runTreePhase(row);
}

void TransferQueue::startFolderScan(int row)
{
    Job &job = m_jobs[row];
    if (!m_scanner) {
        completeJob(row, false, QStringLiteral("Not connected"));
        return;
    }

    job.state = JobState::Scanning;
    job.message = QStringLiteral("Reading folder");
    touch(row);

    m_scanningRow = row;
    m_scanner->scan(job.remotePath);
}

void TransferQueue::buildDownloadPlan(int row)
{
    Job &job = m_jobs[row];

    job.files.clear();
    job.directories.clear();
    job.total = 0;

    const QString root = QDir::cleanPath(job.localRoot);
    const QString rootWithSlash = root.endsWith(QLatin1Char('/')) ? root
                                                                  : root + QLatin1Char('/');
    job.directories.append(root);

    // Every remote name is untrusted, so each mirrored path is rebuilt from
    // sanitised components and confirmed to stay under the chosen folder.
    const auto safeLocalFor = [&](const QString &relative) -> QString {
        QDir base(root);
        QString target = root;
        const QStringList parts = relative.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            const QString safe = path::sanitizeFileName(part);
            if (safe.isEmpty())
                return {};
            target = QDir::cleanPath(target + QLatin1Char('/') + safe);
        }
        Q_UNUSED(base)
        if (target == root || !target.startsWith(rootWithSlash))
            return {};
        return target;
    };

    for (const QString &relative : m_scanner->directories()) {
        const QString local = safeLocalFor(relative);
        if (!local.isEmpty())
            job.directories.append(local);
    }

    int skipped = 0;
    for (const RemoteFile &file : m_scanner->files()) {
        const QString local = safeLocalFor(file.relativePath);
        if (local.isEmpty()) {
            ++skipped;
            continue;
        }
        FilePair pair;
        pair.localPath = local;
        pair.remotePath = file.remotePath;
        pair.size = file.size;
        job.files.append(pair);
        job.total += pair.size;
    }

    if (skipped > 0)
        emit logLine(QStringLiteral("! %1 remote name(s) refused as unsafe").arg(skipped));

    if (job.files.isEmpty()) {
        // An empty folder is still a successful mirror: make it and be done.
        for (const QString &directory : std::as_const(job.directories))
            QDir().mkpath(directory);
        completeJob(row, true, QStringLiteral("Folder is empty"));
        return;
    }

    job.phase = Phase::MakeDirectories;
    job.directoryIndex = 0;
    job.state = JobState::Transferring;
    job.message.clear();
    job.clock.restart();
    touch(row);
    runTreePhase(row);
}

void TransferQueue::runTreePhase(int row)
{
    if (row < 0 || row >= m_jobs.size() || !m_ftp)
        return;

    Job &job = m_jobs[row];
    m_runningRow = row;

    if (job.phase == Phase::MakeDirectories) {
        if (job.kind == Kind::DownloadFolder) {
            // Local directories are made in one go; there is nothing to await.
            for (const QString &directory : std::as_const(job.directories)) {
                if (!QDir().mkpath(directory)) {
                    completeJob(row, false,
                                QStringLiteral("Cannot create %1").arg(directory));
                    return;
                }
            }
            job.phase = Phase::TransferFiles;
            job.fileIndex = 0;
            runTreePhase(row);
            return;
        }

        if (job.directoryIndex < job.directories.size()) {
            const QString directory = job.directories.at(job.directoryIndex);
            job.ftpRequestId = m_ftp->makeDirectory(directory);
            if (job.ftpRequestId == 0)
                completeJob(row, false, QStringLiteral("Not connected"));
            return;
        }

        job.phase = Phase::TransferFiles;
        job.fileIndex = 0;
        job.clock.restart();
    }

    if (job.phase != Phase::TransferFiles)
        return;

    if (job.fileIndex >= job.files.size()) {
        job.done = job.total;
        if (job.kind == Kind::InstallTheme) {
            job.state = JobState::AwaitingDevice;
            job.verified = true;
            job.message = QStringLiteral("%1 files on device").arg(job.files.size());
            cleanupTemp(&job);
            touch(row);
            emit folderInstallReady(job.id, job.title, job.remotePath, job.nextStep);
            m_runningRow = -1;
            emit summaryChanged();
            advance();
            return;
        }
        completeJob(row, true,
                    QStringLiteral("%1 files saved to %2")
                        .arg(job.files.size())
                        .arg(QDir::toNativeSeparators(job.localRoot)));
        return;
    }

    const FilePair &pair = job.files.at(job.fileIndex);
    job.ftpRequestId = job.kind == Kind::InstallTheme
                           ? m_ftp->upload(pair.localPath, pair.remotePath)
                           : m_ftp->download(pair.remotePath, pair.localPath);
    if (job.ftpRequestId == 0)
        completeJob(row, false, QStringLiteral("Not connected"));
}

void TransferQueue::onTreeStepFinished(int row, bool success, const QString &message)
{
    Job &job = m_jobs[row];
    job.ftpRequestId = 0;

    if (job.phase == Phase::MakeDirectories) {
        // A directory that already exists reports failure, which is the normal
        // case on a second install; only a transfer failure matters.
        ++job.directoryIndex;
        runTreePhase(row);
        return;
    }

    if (!success) {
        const QString what = job.fileIndex < job.files.size()
                                 ? path::baseNameOfRemote(job.files.at(job.fileIndex).remotePath)
                                 : QString();
        completeJob(row, false, what.isEmpty() ? message
                                               : QStringLiteral("%1: %2").arg(what, message));
        return;
    }

    if (job.fileIndex < job.files.size())
        job.completedBytes += job.files.at(job.fileIndex).size;
    job.done = job.completedBytes;
    ++job.fileIndex;
    touch(row, { ProgressRole, DoneTextRole, FileProgressRole });
    runTreePhase(row);
}

void TransferQueue::cleanupTemp(Job *job)
{
    if (!job || job->tempRoot.isEmpty())
        return;
    QDir(job->tempRoot).removeRecursively();
    job->tempRoot.clear();
}

void TransferQueue::updateRate(Job *job)
{
    const qint64 elapsed = job->clock.elapsed();
    const qint64 windowMs = elapsed - job->lastSampleMs;
    if (windowMs < 400)
        return;

    const qint64 delta = job->done - job->lastSampleBytes;
    job->rate = static_cast<double>(delta) * 1000.0 / static_cast<double>(windowMs);
    job->lastSampleBytes = job->done;
    job->lastSampleMs = elapsed;
}

void TransferQueue::onTransferProgress(int requestId, qint64 done, qint64 total)
{
    const int row = indexOfRequest(requestId);
    if (row < 0)
        return;

    Job &job = m_jobs[row];
    if (isTreeJob(job)) {
        job.done = job.completedBytes + done;
    } else {
        job.done = done;
        if (total > 0)
            job.total = total;
    }
    updateRate(&job);
    touch(row, { ProgressRole, DoneTextRole, TotalTextRole, RateTextRole });
}

void TransferQueue::onSizeReady(int requestId, const QString &remotePath, qint64 size)
{
    Q_UNUSED(remotePath)
    if (requestId != m_verifyRequestId || m_runningRow < 0)
        return;

    const int row = m_runningRow;
    Job &job = m_jobs[row];
    const qint64 expected = QFileInfo(job.localPath).size();

    if (size < 0) {
        // The server would not tell us; the transfer itself reported success,
        // so report the weaker guarantee rather than inventing a stronger one.
        job.verified = false;
        job.message = QStringLiteral("Size check unavailable");
    } else if (size != expected) {
        // Exact byte counts, not rounded sizes: a message reading "42.0 MB on
        // device, 42.0 MB expected" looks like the app is confused rather than
        // like the transfer is short.
        completeJob(row, false,
                    QStringLiteral("Size mismatch: %1 bytes on device, %2 expected")
                        .arg(size)
                        .arg(expected));
        return;
    } else {
        job.verified = true;
        job.message = job.localSha256.isEmpty()
                          ? QStringLiteral("Size verified")
                          : QStringLiteral("Verified · sha256 %1").arg(job.localSha256.left(12));
    }

    if (job.wantsInstall) {
        job.state = JobState::AwaitingDevice;
        touch(row);
        emit installReady(job.id, job.title, job.titleId, job.remotePath);
        m_runningRow = -1;
        emit summaryChanged();
        advance();
        return;
    }

    completeJob(row, true, job.message);
}

void TransferQueue::onCommandFinished(int requestId, bool success, const QString &message)
{
    if (requestId == m_mkdirRequestId) {
        m_mkdirRequestId = 0;
        return;                                    // "already exists" is fine
    }
    if (requestId == m_verifyRequestId) {
        m_verifyRequestId = 0;
        if (!success && m_connectionLost && m_runningRow >= 0) {
            // The device went away mid-verify; put the job back rather than
            // declaring it done with an unchecked size.
            completeJob(m_runningRow, false, message);
            return;
        }
        if (!success && m_runningRow >= 0) {
            Job &job = m_jobs[m_runningRow];
            job.verified = false;
            job.message = QStringLiteral("Size check unavailable");
            if (job.wantsInstall) {
                job.state = JobState::AwaitingDevice;
                const int row = m_runningRow;
                touch(row);
                emit installReady(job.id, job.title, job.titleId, job.remotePath);
                m_runningRow = -1;
                emit summaryChanged();
                advance();
                return;
            }
            completeJob(m_runningRow, true, job.message);
        }
        return;
    }

    const int row = indexOfRequest(requestId);
    if (row < 0)
        return;

    if (isTreeJob(m_jobs.at(row))) {
        onTreeStepFinished(row, success, message);
        return;
    }

    Job &job = m_jobs[row];
    job.ftpRequestId = 0;

    if (!success) {
        completeJob(row, false, message);
        return;
    }

    if (job.kind == Kind::Download) {
        job.done = job.total > 0 ? job.total : QFileInfo(job.localPath).size();
        job.total = QFileInfo(job.localPath).size();
        completeJob(row, true, QStringLiteral("Saved to %1").arg(QFileInfo(job.localPath).path()));
        return;
    }

    // Upload finished: ask the device how big the file actually is before we
    // let anything install it.
    job.done = job.total;
    job.state = JobState::Verifying;
    touch(row);
    m_verifyRequestId = m_ftp ? m_ftp->requestSize(job.remotePath) : 0;
    if (m_verifyRequestId == 0)
        completeJob(row, true, QStringLiteral("Transferred"));
}

void TransferQueue::completeJob(int row, bool success, const QString &message)
{
    if (row < 0 || row >= m_jobs.size())
        return;

    Job &job = m_jobs[row];

    // A job that was cut off by the device disappearing is not a failed job.
    // Put it back in the queue: the connection is retried automatically, and
    // the work resumes rather than making the user re-drop five photos.
    if (!success && m_connectionLost) {
        m_connectionLost = false;
        job.state = JobState::Pending;
        job.message = QStringLiteral("Waiting for the Vita to come back");
        job.done = 0;
        job.completedBytes = 0;
        job.fileIndex = 0;
        job.directoryIndex = 0;
        job.phase = Phase::None;
        job.rate = 0.0;
        job.ftpRequestId = 0;
        if (isTreeJob(job)) {
            job.files.clear();
            job.directories.clear();
        }
        if (m_runningRow == row)
            m_runningRow = -1;
        touch(row);
        emit jobPostponed(job.id, job.title);
        return;
    }
    m_connectionLost = false;

    job.state = success ? JobState::Complete : JobState::Failed;
    job.message = message;
    cleanupTemp(&job);
    if (m_extractingRow == row)
        m_extractingRow = -1;
    if (m_scanningRow == row) {
        m_scanningRow = -1;
        if (m_scanner)
            m_scanner->cancel();
    }
    if (success && job.total > 0)
        job.done = job.total;
    job.rate = 0.0;
    job.ftpRequestId = 0;

    const int jobId = job.id;
    if (m_runningRow == row)
        m_runningRow = -1;

    touch(row);
    emit jobFinished(jobId, success, message);
    advance();
}

// --- user actions ---------------------------------------------------------

void TransferQueue::cancelJob(int jobId)
{
    const int row = indexOfJob(jobId);
    if (row < 0)
        return;

    Job &job = m_jobs[row];
    if (job.state == JobState::Complete || job.state == JobState::Cancelled)
        return;

    const bool wasRunning = (m_runningRow == row);
    job.state = JobState::Cancelled;
    job.message = QStringLiteral("Cancelled");
    job.rate = 0.0;
    job.ftpRequestId = 0;
    cleanupTemp(&job);

    if (wasRunning) {
        m_runningRow = -1;
        if (m_hashingRow == row)
            m_hashingRow = -1;
        if (m_extractingRow == row)
            m_extractingRow = -1;
        if (m_scanningRow == row) {
            m_scanningRow = -1;
            if (m_scanner)
                m_scanner->cancel();
        }
        if (m_ftp)
            m_ftp->abortAll();
    }

    touch(row);
    advance();
}

void TransferQueue::retryJob(int jobId)
{
    const int row = indexOfJob(jobId);
    if (row < 0)
        return;

    Job &job = m_jobs[row];
    if (job.state != JobState::Failed && job.state != JobState::Cancelled)
        return;

    job.state = JobState::Pending;
    job.done = 0;
    job.rate = 0.0;
    job.verified = false;
    job.message.clear();
    if (isTreeJob(job)) {
        // Re-plan rather than resume: the device may have changed under us.
        job.files.clear();
        job.directories.clear();
        job.phase = Phase::None;
        job.fileIndex = 0;
        job.directoryIndex = 0;
        job.completedBytes = 0;
    }
    touch(row);
    advance();
}

void TransferQueue::clearFinished()
{
    for (int row = static_cast<int>(m_jobs.size()) - 1; row >= 0; --row) {
        const JobState state = m_jobs.at(row).state;
        if (state == JobState::Complete || state == JobState::Cancelled
            || state == JobState::Failed) {
            beginRemoveRows({}, row, row);
            m_jobs.removeAt(row);
            endRemoveRows();
            if (m_runningRow > row)
                --m_runningRow;
            if (m_extractingRow > row)
                --m_extractingRow;
            if (m_scanningRow > row)
                --m_scanningRow;
            if (m_hashingRow > row)
                --m_hashingRow;
        }
    }
    emit summaryChanged();
}

} // namespace vsp
