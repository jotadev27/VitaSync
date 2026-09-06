#pragma once

#include "PackageInspector.h"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
template <typename T> class QFutureWatcher;
QT_END_NAMESPACE

namespace vsp {

class RemoteTransport;
class CompanionClient;
class RemoteTreeScanner;
struct ExtractionResult;

/// The job list behind the Transfers view, and the scheduler that actually
/// runs those jobs against the FTP connection.
///
/// It is a QAbstractListModel so QML can bind straight to it, but the ordering
/// logic lives here rather than in the UI: one job runs at a time, because the
/// Vita's FTP server is single-connection and racing it just produces errors.
///
/// Jobs come in two shapes. A file job moves one file. A tree job moves a whole
/// folder -- a theme going out, a savedata folder coming back -- and runs as a
/// sequence of directory creations followed by one transfer per file, reported
/// to the UI as a single unit of work.
class TransferQueue : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY summaryChanged)
    Q_PROPERTY(int activeCount READ activeCount NOTIFY summaryChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY summaryChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY summaryChanged)
    Q_PROPERTY(bool busy READ isBusy NOTIFY summaryChanged)
    Q_PROPERTY(qreal overallProgress READ overallProgress NOTIFY summaryChanged)

public:
    enum class Kind {
        Upload,
        Download,
        Install,        ///< a package upload that ends in a verified hand-off
        InstallTheme,   ///< a folder sent as a tree: a theme, or an unpacked game
        DownloadFolder  ///< a remote folder mirrored into a local one
    };
    Q_ENUM(Kind)

    enum class JobState {
        Pending,
        Checksumming,
        Unpacking,        ///< a theme archive is being expanded locally
        Scanning,         ///< a remote folder is being walked to find its files
        Transferring,
        Verifying,
        AwaitingDevice,   ///< uploaded and verified; the Vita side needs a tap
        Complete,
        Failed,
        Cancelled
    };
    Q_ENUM(JobState)

    enum Roles {
        JobIdRole = Qt::UserRole + 1,
        KindRole,
        KindLabelRole,
        TitleRole,
        SubtitleRole,
        StateRole,
        StateLabelRole,
        ProgressRole,
        DoneTextRole,
        TotalTextRole,
        RateTextRole,
        MessageRole,
        IconSourceRole,
        RemotePathRole,
        LocalPathRole,
        IsFinishedRole,
        IsFailedRole,
        VerifiedRole,
        FileProgressRole,   ///< "12 / 34 files", empty for single-file jobs
        NextStepRole        ///< what the user has to do next, if anything
    };

    explicit TransferQueue(QObject *parent = nullptr);
    ~TransferQueue() override;

    /// Wires the queue to run jobs against \a ftp. Safe to call again with a
    /// different transport (switching Wi-Fi/USB mode): the previous
    /// connections are dropped first, so nothing double-fires.
    void attach(RemoteTransport *ftp, CompanionClient *companion);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int activeCount() const;
    int pendingCount() const;
    int failedCount() const;
    bool isBusy() const;
    qreal overallProgress() const;

    /// Queues a package upload. When \a install is true the job also verifies
    /// the landed file and hands off to the install step.
    int enqueueUpload(const PackageInfo &info, const QString &remoteDir, bool install);

    /// Queues a plain file upload with no package identity.
    int enqueueRawUpload(const QString &localPath, const QString &remoteDir);

    /// Queues anything that installs as a folder: a theme archive, which is
    /// unpacked locally first, or an already-unpacked game, which is walked
    /// where it lies. Either way the whole tree is sent to
    /// \a remoteParentDir/<folder>.
    int enqueueFolderInstall(const PackageInfo &info, const QString &remoteParentDir);

    int enqueueDownload(const QString &remotePath, qint64 remoteSize,
                        const QString &localDir, const QString &displayName);

    /// Queues a recursive folder download, mirroring the remote tree under
    /// \a localDir.
    int enqueueFolderDownload(const QString &remotePath, const QString &localDir,
                              const QString &displayName);

public slots:
    void cancelJob(int jobId);
    void clearFinished();
    void retryJob(int jobId);
    void start();

signals:
    void summaryChanged();
    void jobFinished(int jobId, bool success, const QString &message);
    /// A job was interrupted by the device going away and has been put back in
    /// the queue; it will resume by itself once the connection returns.
    void jobPostponed(int jobId, const QString &title);

    /// Raised once an install-flagged upload has landed and been verified.
    void installReady(int jobId, const QString &title, const QString &titleId,
                      const QString &remotePath);
    /// Raised once a folder is fully on the device. \a nextStep names the step
    /// the user still has to take, which differs by what was sent: a theme is
    /// applied by one tool, an unpacked game is registered by another.
    void folderInstallReady(int jobId, const QString &title, const QString &remotePath,
                            const QString &nextStep);
    void logLine(const QString &line);

private:
    /// One file inside a tree job.
    struct FilePair
    {
        QString localPath;
        QString remotePath;
        qint64 size = 0;
    };

    enum class Phase
    {
        None,
        MakeDirectories,
        TransferFiles
    };

    struct Job
    {
        int id = 0;
        Kind kind = Kind::Upload;
        JobState state = JobState::Pending;
        Phase phase = Phase::None;

        QString localPath;
        QString remotePath;
        QString remoteDir;
        QString title;
        QString subtitle;
        QString kindLabel;
        QString message;
        QString iconSource;
        QString titleId;
        QString nextStep;

        qint64 total = 0;
        qint64 done = 0;
        double rate = 0.0;
        bool wantsInstall = false;
        bool verified = false;

        QString localSha256;
        QElapsedTimer clock;
        qint64 lastSampleBytes = 0;
        qint64 lastSampleMs = 0;

        int ftpRequestId = 0;

        // Tree jobs only.
        QVector<FilePair> files;
        QStringList directories;      ///< remote (upload) or local (download)
        int fileIndex = 0;
        int directoryIndex = 0;
        qint64 completedBytes = 0;
        QString tempRoot;             ///< extraction dir to remove when done
        bool sourceIsDirectory = false; ///< walk the source instead of unpacking
        QString archivePrefix;
        QString localRoot;            ///< mirror root for a folder download
    };

    int indexOfJob(int jobId) const;
    int indexOfRequest(int requestId) const;
    void appendJob(Job job);
    void touch(int row, const QList<int> &roles = {});
    void advance();
    void beginJob(int row);
    void beginTransfer(int row);
    void completeJob(int row, bool success, const QString &message);
    void updateRate(Job *job);
    QString cacheIconFor(const PackageInfo &info) const;
    static QString stateLabel(const Job &job);
    static bool isTreeJob(const Job &job);

    // Tree jobs.
    void startFolderPreparation(int row);
    void startFolderScan(int row);
    void buildUploadPlan(int row, const ExtractionResult &extraction);
    void buildDownloadPlan(int row);
    void runTreePhase(int row);
    void onTreeStepFinished(int row, bool success, const QString &message);
    void cleanupTemp(Job *job);

    void onCommandFinished(int requestId, bool success, const QString &message);
    void onTransferProgress(int requestId, qint64 done, qint64 total);
    void onSizeReady(int requestId, const QString &remotePath, qint64 size);

    RemoteTransport *m_ftp = nullptr;
    CompanionClient *m_companion = nullptr;
    RemoteTreeScanner *m_scanner = nullptr;
    QList<Job> m_jobs;
    int m_nextJobId = 1;
    int m_runningRow = -1;
    int m_mkdirRequestId = 0;
    int m_verifyRequestId = 0;
    /// Set while a control-channel drop is being processed, so the job that
    /// was in flight is put back in the queue instead of being failed.
    bool m_connectionLost = false;
    /// Remote folders already created this session, to avoid re-issuing MKD.
    QSet<QString> m_createdDirectories;
    QFutureWatcher<QString> *m_hashWatcher = nullptr;
    QFutureWatcher<ExtractionResult> *m_extractWatcher = nullptr;
    int m_hashingRow = -1;
    int m_extractingRow = -1;
    int m_scanningRow = -1;
};

} // namespace vsp
