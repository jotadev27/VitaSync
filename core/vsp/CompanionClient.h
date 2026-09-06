#pragma once

#include <QObject>
#include <QQueue>
#include <QString>

QT_BEGIN_NAMESPACE
class QTcpSocket;
class QTimer;
QT_END_NAMESPACE

namespace vsp {

/// Optional client for the vitacompanion plugin's command server.
///
/// vitacompanion (devnoname120) listens on TCP 1338 and takes newline-
/// terminated text commands. It has no install verb -- there is no such thing
/// in the ecosystem -- but the verbs it does have are worth having: `nosleep`
/// keeps the Vita awake for the length of a long upload, and `launch` brings
/// VitaShell to the front so the user can confirm an install without picking
/// the console up first.
///
/// Everything here is strictly an enhancement. When the plugin is absent the
/// app is fully functional; it simply reports the companion as unavailable.
class CompanionClient : public QObject
{
    Q_OBJECT

public:
    static constexpr quint16 kDefaultPort = 1338;

    explicit CompanionClient(QObject *parent = nullptr);

    bool isAvailable() const { return m_available; }
    QString versionString() const { return m_version; }

    /// Fire-and-check probe: connects, asks for `version`, and reports whether
    /// anything answered. Safe to call on a device without the plugin.
    void probe(const QString &host, quint16 port = kDefaultPort);

    /// Queues a command. Silently does nothing when the plugin is unavailable.
    void sendCommand(const QString &command);

    void keepAwake(bool enabled);
    void launchTitle(const QString &titleId);
    void reboot();

signals:
    void availabilityChanged(bool available);
    void replyReceived(const QString &command, const QString &reply);
    void logLine(const QString &line);

private:
    void openConnection();
    void flushQueue();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_host;
    quint16 m_port = kDefaultPort;
    QString m_version;
    QQueue<QString> m_pending;
    QString m_inFlight;
    QByteArray m_buffer;
    bool m_available = false;
    bool m_probing = false;
};

} // namespace vsp
