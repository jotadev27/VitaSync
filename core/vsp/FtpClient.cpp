#include "FtpClient.h"

#include "PathUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

namespace vsp {
namespace {

// Uploads are fed to the socket in chunks this size as it drains, which keeps
// memory flat regardless of package size.
constexpr qint64 kUploadChunk = 128 * 1024;

bool isPositivePreliminary(int code) { return code >= 100 && code < 200; }
bool isPositiveCompletion(int code)  { return code >= 200 && code < 300; }
bool isPositiveIntermediate(int code){ return code >= 300 && code < 400; }

QDateTime parseListingDate(const QString &month, const QString &day, const QString &yearOrTime)
{
    static const QStringList months = {
        QStringLiteral("Jan"), QStringLiteral("Feb"), QStringLiteral("Mar"),
        QStringLiteral("Apr"), QStringLiteral("May"), QStringLiteral("Jun"),
        QStringLiteral("Jul"), QStringLiteral("Aug"), QStringLiteral("Sep"),
        QStringLiteral("Oct"), QStringLiteral("Nov"), QStringLiteral("Dec")
    };
    const int monthIndex = months.indexOf(month) + 1;
    if (monthIndex == 0)
        return {};

    bool dayOk = false;
    const int dayValue = day.toInt(&dayOk);
    if (!dayOk)
        return {};

    if (yearOrTime.contains(QLatin1Char(':'))) {
        // Within the last six months the server prints a time, not a year.
        const QStringList hm = yearOrTime.split(QLatin1Char(':'));
        if (hm.size() != 2)
            return {};
        const int year = QDate::currentDate().year();
        return QDateTime(QDate(year, monthIndex, dayValue),
                         QTime(hm.at(0).toInt(), hm.at(1).toInt()));
    }

    bool yearOk = false;
    const int year = yearOrTime.toInt(&yearOk);
    if (!yearOk)
        return {};
    return QDateTime(QDate(year, monthIndex, dayValue), QTime(0, 0));
}

} // namespace

FtpClient::FtpClient(QObject *parent)
    : RemoteTransport(parent)
    , m_control(new QTcpSocket(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, &FtpClient::onTimeout);

    connect(m_control, &QTcpSocket::readyRead, this, &FtpClient::onControlReadyRead);
    connect(m_control, &QTcpSocket::errorOccurred, this, &FtpClient::onControlError);
    connect(m_control, &QTcpSocket::disconnected, this, &FtpClient::onControlDisconnected);
    connect(m_control, &QTcpSocket::connected, this, [this] {
        emit logLine(QStringLiteral("< control channel open"));
        m_stage = Stage::AwaitGreeting;
        restartTimeout();
    });
}

FtpClient::~FtpClient()
{
    teardownDataConnection();
}

void FtpClient::setCommandTimeout(int seconds)
{
    m_commandTimeoutMs = qBound(5, seconds, 300) * 1000;
}

void FtpClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}

// --- request entry points -------------------------------------------------

int FtpClient::connectToDevice(const QString &host, quint16 port,
                               const QString &user, const QString &password)
{
    if (!path::isValidIPv4(host) && host.trimmed().isEmpty()) {
        emit errorOccurred(QStringLiteral("Enter the Vita's IP address"));
        return 0;
    }
    if (!path::isValidPort(port)) {
        emit errorOccurred(QStringLiteral("Port must be between 1 and 65535"));
        return 0;
    }

    disconnectFromDevice();

    m_host = host.trimmed();
    m_port = port;

    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Connect;
    request.user = user;
    request.password = password;
    enqueue(request);
    return request.id;
}

void FtpClient::disconnectFromDevice()
{
    abortAll();
    m_timeout->stop();
    if (m_control->state() != QAbstractSocket::UnconnectedState) {
        m_control->abort();
    }
    m_stage = Stage::Idle;
    m_busy = false;
    m_draining = false;
    m_controlBuffer.clear();
    setState(State::Disconnected);
}

int FtpClient::list(const QString &remoteDir)
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

int FtpClient::upload(const QString &localPath, const QString &remotePath)
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

int FtpClient::download(const QString &remotePath, const QString &localPath)
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

int FtpClient::makeDirectory(const QString &remotePath)
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

int FtpClient::removeFile(const QString &remotePath)
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::DeleteFile;
    request.primaryPath = path::normalizeRemote(remotePath);
    enqueue(request);
    return request.id;
}

int FtpClient::removeDirectory(const QString &remotePath)
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

int FtpClient::rename(const QString &fromPath, const QString &toPath)
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

int FtpClient::requestSize(const QString &remotePath)
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

int FtpClient::ping()
{
    if (!isConnected())
        return 0;
    Request request;
    request.id = m_nextRequestId++;
    request.op = Op::Noop;
    enqueue(request);
    return request.id;
}

void FtpClient::abortAll()
{
    const bool hadWork = m_busy || !m_queue.isEmpty();
    m_queue.clear();

    if (m_busy) {
        const int id = m_current.id;

        // A command was sent and the server still owes a reply to it. Closing
        // the data socket ends the transfer, but the "226 Transfer complete"
        // (or "426 aborted") that follows is still coming, and if the next
        // request is allowed to start it will read that reply as its own --
        // every reply after it then belongs to the command before, which is
        // how cancelling one job used to break the next with a message that
        // answered a question nobody had asked.
        const bool replyOwed = (m_stage != Stage::Idle);

        teardownDataConnection();
        m_busy = false;
        m_current = {};

        if (replyOwed && m_control->state() == QAbstractSocket::ConnectedState) {
            m_draining = true;
            m_stage = Stage::Draining;
            emit logLine(QStringLiteral("> (cancelled; waiting for the device to finish replying)"));
            restartTimeout();
        } else {
            m_stage = Stage::Idle;
        }

        emit commandFinished(id, false, QStringLiteral("Cancelled"));
    }

    if (hadWork && isConnected())
        setState(State::Ready);
}

// --- queue ----------------------------------------------------------------

void FtpClient::enqueue(const Request &request)
{
    m_queue.enqueue(request);
    pumpQueue();
}

void FtpClient::pumpQueue()
{
    if (m_busy || m_draining || m_queue.isEmpty())
        return;

    const Request next = m_queue.head();
    // Everything except the initial connect needs a live control channel.
    if (next.op != Op::Connect && !isConnected())
        return;

    m_queue.dequeue();
    m_busy = true;
    m_current = next;
    setState(next.op == Op::Connect ? State::Connecting : State::Busy);
    beginRequest(next);
}

void FtpClient::beginRequest(const Request &request)
{
    m_dataBuffer.clear();
    m_transferDone = 0;
    m_transferTotal = 0;
    m_dataFinished = false;
    m_transferReplySeen = false;
    m_uploadPrimed = false;

    switch (request.op) {
    case Op::Connect:
        emit logLine(QStringLiteral("> connecting to %1:%2").arg(m_host).arg(m_port));
        m_stage = Stage::AwaitGreeting;
        m_control->connectToHost(m_host, m_port);
        restartTimeout();
        break;

    case Op::List:
        // Navigate, then list the working directory. Sending the path as an
        // argument to LIST looks equivalent and is not: VitaShell's server
        // uses the argument only if it stats, and otherwise answers with the
        // working directory instead of an error. A bare device root such as
        // "ux0:" never stats -- it needs the trailing slash that the server's
        // CWD adds and its LIST does not -- so every listing came back as the
        // mount-point list while the client believed it had moved.
        m_stage = Stage::AwaitCwd;
        sendCommand(QStringLiteral("CWD %1").arg(request.primaryPath));
        break;

    case Op::Upload:
    case Op::Download:
        m_stage = Stage::AwaitType;
        sendCommand(QStringLiteral("TYPE I"));
        break;

    case Op::Mkdir:
        m_stage = Stage::AwaitSimple;
        sendCommand(QStringLiteral("MKD %1").arg(request.primaryPath));
        break;

    case Op::DeleteFile:
        m_stage = Stage::AwaitSimple;
        sendCommand(QStringLiteral("DELE %1").arg(request.primaryPath));
        break;

    case Op::DeleteDir:
        m_stage = Stage::AwaitSimple;
        sendCommand(QStringLiteral("RMD %1").arg(request.primaryPath));
        break;

    case Op::Rename:
        m_stage = Stage::AwaitRenameFrom;
        sendCommand(QStringLiteral("RNFR %1").arg(request.primaryPath));
        break;

    case Op::Size:
        m_stage = Stage::AwaitSimple;
        sendCommand(QStringLiteral("SIZE %1").arg(request.primaryPath));
        break;

    case Op::Noop:
        m_stage = Stage::AwaitSimple;
        sendCommand(QStringLiteral("NOOP"));
        break;

    case Op::None:
        finishRequest(false, QStringLiteral("Empty request"));
        break;
    }
}

void FtpClient::finishRequest(bool success, const QString &message)
{
    m_timeout->stop();
    teardownDataConnection();

    const int id = m_current.id;
    const Op op = m_current.op;

    m_busy = false;
    m_stage = Stage::Idle;
    m_current = {};

    if (op == Op::Connect && success) {
        setState(State::Ready);
        emit connected(m_greeting);
    } else if (isConnected()) {
        setState(State::Ready);
    }

    emit commandFinished(id, success, message);
    pumpQueue();
}

void FtpClient::failCurrent(const QString &message)
{
    emit logLine(QStringLiteral("! %1").arg(message));
    if (m_busy)
        finishRequest(false, message);
    else
        emit errorOccurred(message);
}

void FtpClient::restartTimeout()
{
    m_timeout->start(m_commandTimeoutMs);
}

// --- control channel ------------------------------------------------------

void FtpClient::sendCommand(const QString &command, bool redactArgument)
{
    if (m_control->state() != QAbstractSocket::ConnectedState) {
        failCurrent(QStringLiteral("Not connected"));
        return;
    }

    const int space = command.indexOf(QLatin1Char(' '));
    const QString shown = (redactArgument && space > 0)
                              ? command.left(space) + QStringLiteral(" ********")
                              : command;
    emit logLine(QStringLiteral("> %1").arg(shown));

    m_control->write(command.toUtf8() + "\r\n");
    restartTimeout();
}

void FtpClient::onControlReadyRead()
{
    m_controlBuffer.append(m_control->readAll());

    int newline;
    while ((newline = m_controlBuffer.indexOf('\n')) >= 0) {
        QByteArray rawLine = m_controlBuffer.left(newline);
        m_controlBuffer.remove(0, newline + 1);
        if (rawLine.endsWith('\r'))
            rawLine.chop(1);

        const QString line = QString::fromUtf8(rawLine);
        emit logLine(QStringLiteral("< %1").arg(line));

        // A multi-line reply opens with "250-" and closes with "250 ".
        const bool hasCode = line.size() >= 4
                             && line.at(0).isDigit() && line.at(1).isDigit()
                             && line.at(2).isDigit();
        if (!hasCode) {
            m_pendingReplyText.append(line).append(QLatin1Char('\n'));
            continue;
        }

        const int code = line.left(3).toInt();
        const QChar separator = line.at(3);

        if (separator == QLatin1Char('-')) {
            m_multilineCode = code;
            m_pendingReplyText.append(line.mid(4)).append(QLatin1Char('\n'));
            continue;
        }

        if (m_multilineCode != 0 && code != m_multilineCode) {
            m_pendingReplyText.append(line.mid(4)).append(QLatin1Char('\n'));
            continue;
        }

        const QString text = m_pendingReplyText + line.mid(4);
        m_pendingReplyText.clear();
        m_multilineCode = 0;
        handleReply(code, text.trimmed());
    }
}

void FtpClient::handleReply(int code, const QString &text)
{
    m_timeout->stop();

    switch (m_stage) {
    case Stage::Draining:
        // Swallow whatever was still owed. A preliminary reply means the final
        // one is still to come; anything else ends the exchange and the
        // channel is back in step.
        if (isPositivePreliminary(code)) {
            restartTimeout();
            return;
        }
        m_draining = false;
        m_stage = Stage::Idle;
        emit logLine(QStringLiteral("< (channel back in step)"));
        if (isConnected())
            setState(State::Ready);
        pumpQueue();
        return;

    case Stage::AwaitGreeting:
        if (!isPositiveCompletion(code)) {
            failCurrent(QStringLiteral("Device refused the connection: %1").arg(text));
            return;
        }
        m_greeting = text;
        m_stage = Stage::AwaitUser;
        sendCommand(QStringLiteral("USER %1").arg(m_current.user));
        return;

    case Stage::AwaitUser:
        if (isPositiveCompletion(code)) {
            // VitaShell accepts any user and skips straight to logged-in.
            m_stage = Stage::AwaitSyst;
            sendCommand(QStringLiteral("SYST"));
            return;
        }
        if (isPositiveIntermediate(code)) {
            m_stage = Stage::AwaitPass;
            sendCommand(QStringLiteral("PASS %1").arg(m_current.password), true);
            return;
        }
        failCurrent(QStringLiteral("Login rejected: %1").arg(text));
        return;

    case Stage::AwaitPass:
        if (!isPositiveCompletion(code)) {
            failCurrent(QStringLiteral("Login rejected: %1").arg(text));
            return;
        }
        m_stage = Stage::AwaitSyst;
        sendCommand(QStringLiteral("SYST"));
        return;

    case Stage::AwaitSyst:
        // SYST is optional; a refusal is not a failure.
        m_systemType = isPositiveCompletion(code) ? text : QString();
        finishRequest(true, m_greeting);
        return;

    case Stage::AwaitCwd:
        if (!isPositiveCompletion(code)) {
            failCurrent(text.isEmpty() ? QStringLiteral("Cannot open that folder")
                                       : text);
            return;
        }
        m_stage = Stage::AwaitType;
        sendCommand(QStringLiteral("TYPE I"));
        return;

    case Stage::AwaitType:
        if (!isPositiveCompletion(code)) {
            failCurrent(QStringLiteral("Device rejected binary mode: %1").arg(text));
            return;
        }
        startPassiveStep();
        return;

    case Stage::AwaitPasv:
        if (code != 227) {
            failCurrent(QStringLiteral("Passive mode unavailable: %1").arg(text));
            return;
        }
        if (!openDataConnection(text))
            return;
        return;

    case Stage::AwaitTransferStart:
        if (isPositivePreliminary(code)) {
            // The server is ready. For an upload this is the point at which
            // writing may begin; for a download the bytes simply arrive.
            restartTimeout();
            if (m_current.op == Op::Upload && m_data && !m_uploadPrimed) {
                m_uploadPrimed = true;
                onDataBytesWritten(0);
            }
            return;
        }
        if (isPositiveCompletion(code)) {
            // Some servers answer an empty transfer with 226 straight away.
            m_stage = Stage::AwaitTransferEnd;
            m_transferReplySeen = true;
            if (m_dataFinished)
                handleReply(code, text);
            return;
        }
        failCurrent(QStringLiteral("Transfer refused: %1").arg(text));
        return;

    case Stage::AwaitTransferEnd: {
        // A small transfer can finish and close its data socket before the
        // server's "150 opening data connection" has even been read. That
        // preliminary reply belongs to this transfer, not to a failure.
        if (isPositivePreliminary(code)) {
            restartTimeout();
            return;
        }
        if (!isPositiveCompletion(code)) {
            failCurrent(QStringLiteral("Transfer failed: %1").arg(text));
            return;
        }
        m_transferReplySeen = true;
        if (!m_dataFinished) {
            // Wait for the data socket to flush before declaring success.
            restartTimeout();
            return;
        }

        if (m_current.op == Op::List) {
            const QVector<RemoteEntry> entries = parseListing(m_dataBuffer, m_current.primaryPath);
            emit listingReady(m_current.id, m_current.primaryPath, entries);
        }
        finishRequest(true, text);
        return;
    }

    case Stage::AwaitRenameFrom:
        if (!isPositiveIntermediate(code)) {
            failCurrent(QStringLiteral("Rename source rejected: %1").arg(text));
            return;
        }
        m_stage = Stage::AwaitRenameTo;
        sendCommand(QStringLiteral("RNTO %1").arg(m_current.secondaryPath));
        return;

    case Stage::AwaitRenameTo:
    case Stage::AwaitSimple:
        if (!isPositiveCompletion(code)) {
            failCurrent(text.isEmpty() ? QStringLiteral("Command failed") : text);
            return;
        }
        if (m_current.op == Op::Size) {
            bool ok = false;
            const qint64 size = text.trimmed().toLongLong(&ok);
            emit sizeReady(m_current.id, m_current.primaryPath, ok ? size : -1);
        }
        finishRequest(true, text);
        return;

    case Stage::Idle:
        // Unsolicited message (e.g. the server timing us out). Nothing to do.
        emit logLine(QStringLiteral("< unsolicited %1 %2").arg(code).arg(text));
        return;
    }
}

void FtpClient::startPassiveStep()
{
    m_stage = Stage::AwaitPasv;
    sendCommand(QStringLiteral("PASV"));
}

bool FtpClient::openDataConnection(const QString &pasvReply)
{
    // 227 Entering Passive Mode (192,168,1,40,5,57)
    static const QRegularExpression re(
        QStringLiteral("(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})"
                       "\\s*,\\s*(\\d{1,3})\\s*,\\s*(\\d{1,3})"));
    const QRegularExpressionMatch match = re.match(pasvReply);
    if (!match.hasMatch()) {
        failCurrent(QStringLiteral("Could not read the passive-mode reply"));
        return false;
    }

    const QString advertisedHost = QStringLiteral("%1.%2.%3.%4")
                                       .arg(match.captured(1), match.captured(2),
                                            match.captured(3), match.captured(4));
    const int dataPort = match.captured(5).toInt() * 256 + match.captured(6).toInt();
    if (!path::isValidPort(dataPort)) {
        failCurrent(QStringLiteral("Device advertised an invalid data port"));
        return false;
    }

    // Trust the control channel's peer over the advertised address: a Vita
    // behind a router may report an address we cannot reach, and we must never
    // be redirected to a third host by a malformed reply.
    QString dataHost = m_control->peerAddress().toString();
    if (dataHost.isEmpty())
        dataHost = advertisedHost;

    teardownDataConnection();

    m_data = new QTcpSocket(this);
    connect(m_data, &QTcpSocket::connected, this, &FtpClient::onDataConnected);
    connect(m_data, &QTcpSocket::readyRead, this, &FtpClient::onDataReadyRead);
    connect(m_data, &QTcpSocket::disconnected, this, &FtpClient::onDataDisconnected);
    connect(m_data, &QTcpSocket::errorOccurred, this, &FtpClient::onDataError);
    connect(m_data, &QTcpSocket::bytesWritten, this, &FtpClient::onDataBytesWritten);

    emit logLine(QStringLiteral("> data channel %1:%2").arg(dataHost).arg(dataPort));
    m_data->connectToHost(dataHost, static_cast<quint16>(dataPort));
    restartTimeout();
    return true;
}

void FtpClient::onDataConnected()
{
    m_stage = Stage::AwaitTransferStart;

    switch (m_current.op) {
    case Op::List:
        // No argument: the working directory was set by CWD above, and that is
        // the only form of LIST every server agrees on.
        sendCommand(QStringLiteral("LIST"));
        break;

    case Op::Download: {
        const QString target = m_current.localPath;
        m_transferFile = new QFile(target, this);
        if (!m_transferFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            failCurrent(QStringLiteral("Cannot write to %1").arg(target));
            return;
        }
        sendCommand(QStringLiteral("RETR %1").arg(m_current.primaryPath));
        break;
    }

    case Op::Upload: {
        m_transferFile = new QFile(m_current.localPath, this);
        if (!m_transferFile->open(QIODevice::ReadOnly)) {
            failCurrent(QStringLiteral("Cannot read %1").arg(m_current.localPath));
            return;
        }
        m_transferTotal = m_transferFile->size();
        sendCommand(QStringLiteral("STOR %1").arg(m_current.primaryPath));
        // Nothing is written yet. RFC 959 has the client wait for the
        // preliminary reply, and it matters in practice: a small file can be
        // written and its data socket closed before the server has even read
        // the STOR, at which point the server sees a connection that opened
        // and vanished and answers nothing at all.
        break;
    }

    default:
        break;
    }
}

void FtpClient::onDataReadyRead()
{
    const QByteArray chunk = m_data->readAll();
    if (chunk.isEmpty())
        return;

    restartTimeout();

    if (m_current.op == Op::Download && m_transferFile) {
        if (m_transferFile->write(chunk) != chunk.size()) {
            failCurrent(QStringLiteral("Ran out of space while writing the file"));
            return;
        }
        m_transferDone += chunk.size();
        emit transferProgress(m_current.id, m_transferDone, m_transferTotal);
    } else {
        // Directory listings are small enough to hold in memory; cap anyway.
        if (m_dataBuffer.size() < 8 * 1024 * 1024)
            m_dataBuffer.append(chunk);
    }
}

void FtpClient::onDataBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes)
    if (m_current.op != Op::Upload || !m_transferFile || !m_data)
        return;
    if (!m_uploadPrimed)
        return;                             // still waiting for the go-ahead

    // Keep at most one chunk queued so progress tracks the wire, not the buffer.
    if (m_data->bytesToWrite() > kUploadChunk)
        return;

    if (m_transferFile->atEnd()) {
        if (m_data->bytesToWrite() == 0 && m_data->state() == QAbstractSocket::ConnectedState) {
            m_data->disconnectFromHost();
        }
        return;
    }

    const QByteArray chunk = m_transferFile->read(kUploadChunk);
    if (chunk.isEmpty()) {
        m_data->disconnectFromHost();
        return;
    }

    const qint64 written = m_data->write(chunk);
    if (written < 0) {
        failCurrent(QStringLiteral("Connection dropped during upload"));
        return;
    }

    m_transferDone += written;
    emit transferProgress(m_current.id, m_transferDone, m_transferTotal);
    restartTimeout();
}

void FtpClient::onDataDisconnected()
{
    m_dataFinished = true;

    if (m_transferFile) {
        m_transferFile->flush();
        m_transferFile->close();
    }

    if (m_stage == Stage::AwaitTransferStart) {
        m_stage = Stage::AwaitTransferEnd;
        restartTimeout();
        return;
    }

    if (m_stage == Stage::AwaitTransferEnd && m_transferReplySeen) {
        if (m_current.op == Op::List) {
            const QVector<RemoteEntry> entries = parseListing(m_dataBuffer, m_current.primaryPath);
            emit listingReady(m_current.id, m_current.primaryPath, entries);
        }
        finishRequest(true, QStringLiteral("Transfer complete"));
    }
}

void FtpClient::onDataError(QAbstractSocket::SocketError error)
{
    if (error == QAbstractSocket::RemoteHostClosedError)
        return;                                   // normal end of a transfer
    if (!m_busy)
        return;
    failCurrent(QStringLiteral("Data channel error: %1").arg(m_data ? m_data->errorString() : QString()));
}

void FtpClient::teardownDataConnection()
{
    if (m_data) {
        m_data->disconnect(this);
        m_data->abort();
        m_data->deleteLater();
        m_data = nullptr;
    }
    if (m_transferFile) {
        m_transferFile->close();
        m_transferFile->deleteLater();
        m_transferFile = nullptr;
    }
}

void FtpClient::handleConnectionLoss(const QString &message)
{
    // A dropped socket usually reports twice -- an error and then a
    // disconnect -- and the two used to race. The error path marked the client
    // disconnected first, which meant the disconnect path then decided nothing
    // had been connected and stayed silent, so nobody upstream ever learned to
    // reconnect. One entry point, one teardown, one set of signals.
    if (m_handlingLoss)
        return;
    m_handlingLoss = true;

    const bool wasEstablished = (m_state == State::Ready || m_state == State::Busy);
    const bool wasActive = (m_state != State::Disconnected);

    m_timeout->stop();
    teardownDataConnection();

    // Stop accepting work before anything else runs: failing the in-flight
    // request pumps the queue, and without this that pump would write the next
    // command straight into a socket that is already gone.
    m_state = State::Disconnected;

    const bool hadRequest = m_busy;
    const int requestId = m_current.id;
    m_busy = false;
    m_stage = Stage::Idle;
    m_current = {};
    m_queue.clear();
    m_controlBuffer.clear();
    m_pendingReplyText.clear();
    m_multilineCode = 0;
    m_draining = false;

    emit logLine(QStringLiteral("! control channel: %1").arg(message));

    if (hadRequest) {
        // Cause before effect, so the owner of the request can tell "the
        // device went away" from "the device refused this".
        emit connectionLost();
        emit commandFinished(requestId, false, message);
    } else {
        emit errorOccurred(message);
    }

    if (wasActive)
        emit stateChanged(State::Disconnected);
    if (wasEstablished)
        emit disconnected();

    m_handlingLoss = false;
}

void FtpClient::onControlError(QAbstractSocket::SocketError error)
{
    Q_UNUSED(error)
    handleConnectionLoss(m_control->errorString());
}

void FtpClient::onControlDisconnected()
{
    handleConnectionLoss(QStringLiteral("Connection closed"));
}

void FtpClient::onTimeout()
{
    if (m_draining) {
        // The device never finished answering the cancelled command, so the
        // channel is of unknown shape. Drop it and let the reconnect give us a
        // clean one rather than guessing where the replies line up.
        m_draining = false;
        m_stage = Stage::Idle;
        handleConnectionLoss(QStringLiteral("Lost track of the device after a cancel"));
        m_control->abort();
        return;
    }

    failCurrent(QStringLiteral("The Vita stopped responding"));
    m_control->abort();
}

// --- listing parser -------------------------------------------------------

QVector<RemoteEntry> FtpClient::parseListing(const QByteArray &raw, const QString &remoteDir)
{
    QVector<RemoteEntry> entries;
    const QStringList lines = QString::fromUtf8(raw).split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                                           Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        // Unix-style long listing, which is what VitaShell's FTP server emits:
        //   drwxrwxrwx 1 vita vita     0 Jan 01  1970 folder name
        //   -rw-rw-rw- 1 vita vita 92841 Mar 14 21:07 file.vpk
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (fields.size() < 9)
            continue;

        RemoteEntry entry;
        entry.permissions = fields.at(0);
        entry.isDirectory = entry.permissions.startsWith(QLatin1Char('d'));

        bool sizeOk = false;
        const qint64 size = fields.at(4).toLongLong(&sizeOk);
        entry.size = entry.isDirectory ? -1 : (sizeOk ? size : -1);
        entry.modified = parseListingDate(fields.at(5), fields.at(6), fields.at(7));

        // The name is everything after the timestamp, so names with spaces
        // survive intact. Rebuild it by offset rather than by re-joining
        // fields, which would collapse runs of spaces.
        int consumed = 0;
        int cursor = 0;
        while (cursor < line.size() && consumed < 8) {
            while (cursor < line.size() && line.at(cursor).isSpace())
                ++cursor;
            while (cursor < line.size() && !line.at(cursor).isSpace())
                ++cursor;
            ++consumed;
        }
        while (cursor < line.size() && line.at(cursor).isSpace())
            ++cursor;
        entry.name = line.mid(cursor);

        if (entry.name.isEmpty()
            || entry.name == QLatin1String(".")
            || entry.name == QLatin1String("..")) {
            continue;
        }
        // Never let a server-supplied name carry separators into our paths.
        if (!path::isSafeComponent(entry.name))
            continue;

        entry.path = path::joinRemote(remoteDir, entry.name);
        entries.append(entry);
    }

    std::sort(entries.begin(), entries.end(), [](const RemoteEntry &a, const RemoteEntry &b) {
        if (a.isDirectory != b.isDirectory)
            return a.isDirectory;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });

    return entries;
}

} // namespace vsp
