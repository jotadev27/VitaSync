#include "CompanionClient.h"

#include "PathUtils.h"

#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace vsp {
namespace {

// The command server takes plain text. Anything that could smuggle a second
// command (a newline) or arrive as a control byte is rejected outright rather
// than escaped -- none of our verbs need those characters.
bool isSafeCommand(const QString &command)
{
    if (command.isEmpty() || command.size() > 256)
        return false;
    for (const QChar c : command) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    }
    return true;
}

} // namespace

CompanionClient::CompanionClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(4000);

    connect(m_timeout, &QTimer::timeout, this, [this] {
        if (m_probing) {
            m_probing = false;
            if (m_available) {
                m_available = false;
                emit availabilityChanged(false);
            }
        }
        m_socket->abort();
    });

    connect(m_socket, &QTcpSocket::connected, this, [this] {
        emit logLine(QStringLiteral("companion: connected"));
        flushQueue();
    });

    connect(m_socket, &QTcpSocket::readyRead, this, [this] {
        m_buffer.append(m_socket->readAll());
        int newline;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            QByteArray rawLine = m_buffer.left(newline);
            m_buffer.remove(0, newline + 1);
            if (rawLine.endsWith('\r'))
                rawLine.chop(1);

            const QString reply = QString::fromUtf8(rawLine).trimmed();
            if (reply.isEmpty())
                continue;

            emit logLine(QStringLiteral("companion < %1").arg(reply));
            emit replyReceived(m_inFlight, reply);

            if (m_probing) {
                m_probing = false;
                m_timeout->stop();
                m_version = reply;
                if (!m_available) {
                    m_available = true;
                    emit availabilityChanged(true);
                }
            }
        }
        flushQueue();
    });

    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit logLine(QStringLiteral("companion: %1").arg(m_socket->errorString()));
        m_timeout->stop();
        m_pending.clear();
        if (m_probing) {
            m_probing = false;
            if (m_available) {
                m_available = false;
                emit availabilityChanged(false);
            }
        }
    });
}

void CompanionClient::probe(const QString &host, quint16 port)
{
    if (!path::isValidIPv4(host) || !path::isValidPort(port))
        return;

    m_host = host;
    m_port = port;
    m_probing = true;
    m_pending.clear();
    m_pending.enqueue(QStringLiteral("version"));
    m_timeout->start();
    openConnection();
}

void CompanionClient::sendCommand(const QString &command)
{
    if (!isSafeCommand(command))
        return;
    if (m_host.isEmpty())
        return;

    m_pending.enqueue(command);
    m_timeout->start();
    openConnection();
}

void CompanionClient::keepAwake(bool enabled)
{
    sendCommand(enabled ? QStringLiteral("nosleep on") : QStringLiteral("nosleep off"));
}

void CompanionClient::launchTitle(const QString &titleId)
{
    // Title IDs are alphanumeric; refuse anything else instead of quoting it.
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9]{4,16}$"));
    if (!re.match(titleId).hasMatch())
        return;
    sendCommand(QStringLiteral("launch %1").arg(titleId));
}

void CompanionClient::reboot()
{
    sendCommand(QStringLiteral("reboot"));
}

void CompanionClient::openConnection()
{
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        flushQueue();
        return;
    }
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        return;                                   // already dialling

    m_buffer.clear();
    m_socket->connectToHost(m_host, m_port);
}

void CompanionClient::flushQueue()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState)
        return;

    while (!m_pending.isEmpty()) {
        m_inFlight = m_pending.dequeue();
        emit logLine(QStringLiteral("companion > %1").arg(m_inFlight));
        m_socket->write(m_inFlight.toUtf8() + "\n");
    }
    m_socket->flush();
}

} // namespace vsp
