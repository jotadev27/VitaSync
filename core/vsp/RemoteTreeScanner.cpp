#include "RemoteTreeScanner.h"

#include "PathUtils.h"
#include "RemoteTransport.h"

namespace vsp {

RemoteTreeScanner::RemoteTreeScanner(RemoteTransport *ftp, QObject *parent)
    : QObject(parent)
    , m_ftp(ftp)
{
    if (!m_ftp)
        return;

    connect(m_ftp, &RemoteTransport::listingReady, this, &RemoteTreeScanner::onListingReady);
    connect(m_ftp, &RemoteTransport::commandFinished, this, &RemoteTreeScanner::onCommandFinished);
}

void RemoteTreeScanner::scan(const QString &rootPath, const ScanLimits &limits)
{
    if (m_running || !m_ftp)
        return;

    m_limits = limits;
    m_queue.clear();
    m_files.clear();
    m_directories.clear();
    m_totalBytes = 0;
    m_directoriesDone = 0;
    m_requestId = 0;
    m_root = path::normalizeRemote(rootPath);
    m_running = true;

    m_queue.enqueue(Pending { m_root, QString(), 0 });
    requestNext();
}

void RemoteTreeScanner::cancel()
{
    if (!m_running)
        return;
    m_queue.clear();
    m_requestId = 0;
    finish(false, QStringLiteral("Cancelled"));
}

void RemoteTreeScanner::requestNext()
{
    if (!m_running)
        return;

    if (m_queue.isEmpty()) {
        finish(true, QStringLiteral("%1 files").arg(m_files.size()));
        return;
    }

    m_current = m_queue.dequeue();
    m_requestId = m_ftp->list(m_current.path);
    if (m_requestId == 0)
        finish(false, QStringLiteral("Not connected"));
}

void RemoteTreeScanner::onListingReady(int requestId, const QString &remoteDir,
                                       const QVector<RemoteEntry> &entries)
{
    Q_UNUSED(remoteDir)
    if (!m_running || requestId != m_requestId)
        return;

    m_requestId = 0;
    ++m_directoriesDone;

    // Record the directory itself so an empty one still gets mirrored.
    if (!m_current.relative.isEmpty())
        m_directories.append(m_current.relative);

    for (const RemoteEntry &entry : entries) {
        const QString relative = m_current.relative.isEmpty()
                                     ? entry.name
                                     : m_current.relative + QLatin1Char('/') + entry.name;

        if (entry.isDirectory) {
            if (m_current.depth + 1 > m_limits.maxDepth)
                continue;
            m_queue.enqueue(Pending { entry.path, relative, m_current.depth + 1 });
            continue;
        }

        if (m_files.size() >= m_limits.maxFiles) {
            finish(false, QStringLiteral("Folder holds more than %1 files")
                              .arg(m_limits.maxFiles));
            return;
        }

        RemoteFile file;
        file.remotePath = entry.path;
        file.relativePath = relative;
        file.size = qMax<qint64>(entry.size, 0);
        m_files.append(file);
        m_totalBytes += file.size;
    }

    emit scanProgress(m_directoriesDone, static_cast<int>(m_files.size()));
    requestNext();
}

void RemoteTreeScanner::onCommandFinished(int requestId, bool success, const QString &message)
{
    if (!m_running || requestId != m_requestId)
        return;

    // A listing that fails mid-walk is reported rather than silently producing
    // a partial mirror the user would think was complete.
    if (!success) {
        m_requestId = 0;
        finish(false, QStringLiteral("Could not read %1: %2").arg(m_current.path, message));
    }
}

void RemoteTreeScanner::finish(bool ok, const QString &message)
{
    m_running = false;
    m_requestId = 0;
    m_queue.clear();
    emit finished(ok, message);
}

} // namespace vsp
