#pragma once

#include "RemoteTransport.h"

#include <QAbstractSocket>
#include <QDateTime>
#include <QQueue>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QFile;
class QTcpSocket;
class QTimer;
QT_END_NAMESPACE

namespace vsp {

/// Asynchronous FTP client aimed at the server VitaShell exposes, and the
/// Wi-Fi implementation of `RemoteTransport`.
///
/// Every request is queued and answered with a matching `commandFinished`
/// carrying the request id, so callers never have to guess which reply belongs
/// to them. Nothing here blocks: uploads are fed to the socket as it drains, so
/// a 4 GB package never sits in memory.
class FtpClient : public RemoteTransport
{
    Q_OBJECT

public:
    using State = RemoteTransport::State;

    explicit FtpClient(QObject *parent = nullptr);
    ~FtpClient() override;

    State state() const override { return m_state; }
    bool isConnected() const override { return m_state == State::Ready || m_state == State::Busy; }
    QString host() const { return m_host; }
    quint16 port() const { return m_port; }
    QString serverGreeting() const { return m_greeting; }
    QString systemType() const { return m_systemType; }

    /// Seconds a command may sit unanswered before we give up on it.
    void setCommandTimeout(int seconds);

    /// Parses a unix-style LIST response into entries. Pure function, exposed
    /// so it can be tested without a device on the other end of a socket.
    static QVector<RemoteEntry> parseListing(const QByteArray &raw, const QString &remoteDir);

    // --- requests -------------------------------------------------------
    // Each returns a request id (> 0), or 0 when the request was rejected
    // outright (bad input, not connected).

    int connectToDevice(const QString &host, quint16 port,
                        const QString &user = QStringLiteral("anonymous"),
                        const QString &password = QStringLiteral("vitasync@local"));
    void disconnectFromDevice();

    int list(const QString &remoteDir) override;
    int upload(const QString &localPath, const QString &remotePath) override;
    int download(const QString &remotePath, const QString &localPath) override;
    int makeDirectory(const QString &remotePath) override;
    int removeFile(const QString &remotePath) override;
    int removeDirectory(const QString &remotePath) override;
    int rename(const QString &fromPath, const QString &toPath) override;
    int requestSize(const QString &remotePath) override;
    int ping() override;

    /// Cancels the transfer in flight (if any) and clears anything still queued.
    void abortAll() override;

private slots:
    void onControlReadyRead();
    void onControlError(QAbstractSocket::SocketError error);
    void onControlDisconnected();
    void onDataConnected();
    void onDataReadyRead();
    void onDataDisconnected();
    void onDataError(QAbstractSocket::SocketError error);
    void onDataBytesWritten(qint64 bytes);
    void onTimeout();

private:
    enum class Op {
        None, Connect, List, Upload, Download, Mkdir, DeleteFile,
        DeleteDir, Rename, Size, Noop
    };

    enum class Stage {
        Idle, Draining, AwaitGreeting, AwaitUser, AwaitPass, AwaitSyst, AwaitCwd, AwaitType,
        AwaitPasv, AwaitTransferStart, AwaitTransferEnd, AwaitSimple,
        AwaitRenameFrom, AwaitRenameTo
    };

    struct Request
    {
        int id = 0;
        Op op = Op::None;
        QString primaryPath;
        QString secondaryPath;
        QString localPath;
        QString user;
        QString password;
    };

    void enqueue(const Request &request);
    void pumpQueue();
    void beginRequest(const Request &request);
    void finishRequest(bool success, const QString &message);
    void setState(State state);

    void sendCommand(const QString &command, bool redactArgument = false);
    void handleReply(int code, const QString &text);
    bool openDataConnection(const QString &pasvReply);
    void startPassiveStep();
    void teardownDataConnection();
    void failCurrent(const QString &message);
    /// The one place a lost control channel is dealt with. Both the socket
    /// error and the socket disconnect funnel through here, so the teardown
    /// happens once, in one order, whichever arrives first.
    void handleConnectionLoss(const QString &message);
    void restartTimeout();



    QTcpSocket *m_control = nullptr;
    QTcpSocket *m_data = nullptr;
    QTimer *m_timeout = nullptr;

    QString m_host;
    quint16 m_port = 1337;
    QString m_greeting;
    QString m_systemType;

    State m_state = State::Disconnected;
    Stage m_stage = Stage::Idle;

    QQueue<Request> m_queue;
    Request m_current;
    bool m_busy = false;

    QByteArray m_controlBuffer;
    QByteArray m_dataBuffer;
    QString m_pendingReplyText;
    int m_multilineCode = 0;

    QFile *m_transferFile = nullptr;
    qint64 m_transferTotal = 0;
    qint64 m_transferDone = 0;
    bool m_dataFinished = false;
    bool m_transferReplySeen = false;
    /// Set once the server has acknowledged STOR and it is safe to write.
    bool m_uploadPrimed = false;
    bool m_handlingLoss = false;
    /// True while replies owed for an abandoned command are being swallowed.
    bool m_draining = false;

    int m_nextRequestId = 1;
    int m_commandTimeoutMs = 20000;
};

} // namespace vsp
