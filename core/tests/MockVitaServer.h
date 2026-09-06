#pragma once

#include <QDir>
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

/// A stand-in for the FTP server VitaShell runs, backed by a real directory.
///
/// It speaks the same narrow dialect the Vita does -- passive mode only, unix
/// long listings, no authentication to speak of -- which is exactly what the
/// client has to cope with. Having it in-process means the transport can be
/// tested end to end without a console on the desk.
class MockVitaServer : public QObject
{
    Q_OBJECT

public:
    explicit MockVitaServer(const QString &rootPath, QObject *parent = nullptr);
    ~MockVitaServer() override;

    /// Listens on localhost. Port 0 picks a free one.
    bool listen(quint16 port = 0);
    quint16 port() const;

    /// Makes SIZE report a wrong value, to exercise the client's verification.
    void setCorruptSizeReports(bool corrupt) { m_corruptSize = corrupt; }

    /// Drops the control connection after \a count completed uploads, the way
    /// a device that has run out of something does. 0 disables it.
    void setDropControlAfterUploads(int count) { m_dropAfterUploads = count; }
    int completedUploads() const { return m_completedUploads; }
    int listCount() const { return m_listCount; }

    /// Every control command received, in order, so a test can assert on the
    /// protocol the client actually speaks rather than only on the result.
    QStringList commandLog() const { return m_commandLog; }
    void clearCommandLog() { m_commandLog.clear(); }

private:
    void handleConnection();
    void handleLine(QTcpSocket *control, const QString &line);
    void reply(QTcpSocket *control, int code, const QString &text);
    QString resolve(const QString &remotePath) const;
    QByteArray buildListing(const QString &localDir) const;
    QByteArray buildDeviceListing() const;

    /// libftpvita's `file_exists(get_vita_path(path))`: a plain stat, with no
    /// trailing-slash fixup. A bare device root such as "ux0:" does not stat
    /// on a real Vita, which is the whole reason LIST-with-a-path is unsafe.
    bool vitaPathStats(const QString &remotePath) const;
    /// libftpvita's CWD path building, including the device-root fixup.
    bool resolveForCwd(const QString &current, const QString &argument,
                       QString *resolved) const;
    bool openPassivePort(QTcpSocket *control);
    void startDataTransfer(QTcpSocket *control);
    void finishUpload(QTcpSocket *control);

    struct Session
    {
        QTcpServer *passive = nullptr;
        QTcpSocket *data = nullptr;
        /// The server-side working directory, exactly as libftpvita keeps it.
        /// Starts at "/", which lists the devices rather than any real folder.
        QString currentPath = QStringLiteral("/");
        QString pendingCommand;
        QString pendingArgument;
        QString renameFrom;
        QByteArray outgoing;
        QString incomingPath;
        QByteArray incoming;
        bool receiving = false;
        bool dataClosed = false;
    };

    QTcpServer m_control;
    QDir m_root;
    QHash<QTcpSocket *, Session *> m_sessions;
    bool m_corruptSize = false;
    int m_dropAfterUploads = 0;
    int m_completedUploads = 0;
    int m_listCount = 0;
    QStringList m_commandLog;
};
