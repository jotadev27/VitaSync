#pragma once

#include <QObject>
#include <QQueue>
#include <QString>
#include <QVector>

namespace vsp {

class RemoteTransport;

/// One file found while walking a remote folder.
struct RemoteFile
{
    QString remotePath;
    QString relativePath;   ///< path below the scan root, '/' separated
    qint64 size = 0;
};

/// Walks a remote directory tree breadth-first over an existing FTP connection.
///
/// Folder download needs the full file list before it can report a total or a
/// percentage, and the Vita's server only answers one command at a time, so the
/// walk is a queue of LIST requests rather than recursion. Empty directories
/// are recorded too, so a mirrored copy keeps its shape.
/// Ceilings for a walk, so a pathological tree cannot spin forever.
struct ScanLimits
{
    int maxFiles = 20000;
    int maxDepth = 12;
};

class RemoteTreeScanner : public QObject
{
    Q_OBJECT

public:
    explicit RemoteTreeScanner(RemoteTransport *ftp, QObject *parent = nullptr);

    bool isRunning() const { return m_running; }

    /// Starts a walk of \a rootPath. Only one walk runs at a time.
    void scan(const QString &rootPath, const ScanLimits &limits = ScanLimits());
    void cancel();

    const QVector<RemoteFile> &files() const { return m_files; }
    const QStringList &directories() const { return m_directories; }
    qint64 totalBytes() const { return m_totalBytes; }

signals:
    /// Progress while walking, so the UI can show something during a long scan.
    void scanProgress(int directoriesDone, int filesFound);
    void finished(bool ok, const QString &message);

private:
    struct Pending
    {
        QString path;
        QString relative;
        int depth = 0;
    };

    void requestNext();
    void onListingReady(int requestId, const QString &remoteDir,
                        const QVector<struct RemoteEntry> &entries);
    void onCommandFinished(int requestId, bool success, const QString &message);
    void finish(bool ok, const QString &message);

    RemoteTransport *m_ftp = nullptr;
    ScanLimits m_limits;

    QQueue<Pending> m_queue;
    Pending m_current;
    int m_requestId = 0;
    int m_directoriesDone = 0;

    QVector<RemoteFile> m_files;
    QStringList m_directories;
    qint64 m_totalBytes = 0;
    QString m_root;
    bool m_running = false;
};

} // namespace vsp
