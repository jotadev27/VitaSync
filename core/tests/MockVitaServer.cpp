#include "MockVitaServer.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTimer>
#include <QRegularExpression>

MockVitaServer::MockVitaServer(const QString &rootPath, QObject *parent)
    : QObject(parent)
    , m_root(rootPath)
{
    connect(&m_control, &QTcpServer::newConnection, this, &MockVitaServer::handleConnection);
}

MockVitaServer::~MockVitaServer()
{
    // Child sockets are destroyed after this body runs, and closing one then
    // fires the disconnected handler against a session table that is already
    // being torn down. Detach and drop the sessions first.
    const QList<QTcpSocket *> sockets = m_sessions.keys();
    for (QTcpSocket *socket : sockets) {
        socket->disconnect(this);
        socket->abort();
    }
    qDeleteAll(m_sessions);
    m_sessions.clear();
}

bool MockVitaServer::listen(quint16 port)
{
    return m_control.listen(QHostAddress::LocalHost, port);
}

quint16 MockVitaServer::port() const
{
    return m_control.serverPort();
}

void MockVitaServer::handleConnection()
{
    QTcpSocket *control = m_control.nextPendingConnection();
    if (!control)
        return;

    auto *session = new Session;
    m_sessions.insert(control, session);

    connect(control, &QTcpSocket::readyRead, this, [this, control] {
        while (control->canReadLine()) {
            QByteArray raw = control->readLine();
            while (raw.endsWith('\n') || raw.endsWith('\r'))
                raw.chop(1);
            handleLine(control, QString::fromUtf8(raw));
        }
    });

    connect(control, &QTcpSocket::disconnected, this, [this, control] {
        Session *session = m_sessions.take(control);
        if (session) {
            if (session->passive)
                session->passive->deleteLater();
            if (session->data)
                session->data->deleteLater();
            delete session;
        }
        control->deleteLater();
    });

    reply(control, 220, QStringLiteral("VitaShell 2.02 FTP ready"));
}

void MockVitaServer::reply(QTcpSocket *control, int code, const QString &text)
{
    control->write(QStringLiteral("%1 %2\r\n").arg(code).arg(text).toUtf8());
    control->flush();
}

QString MockVitaServer::resolve(const QString &remotePath) const
{
    // The Vita's paths look like "/ux0:/vpk"; map that onto a real directory.
    QString relative = remotePath;
    relative.remove(QRegularExpression(QStringLiteral("^/")));
    return m_root.filePath(relative);
}

bool MockVitaServer::vitaPathStats(const QString &remotePath) const
{
    // "/" is not a real directory on the device; it is the synthetic root that
    // lists mount points.
    if (remotePath == QLatin1String("/"))
        return false;

    QString vitaPath = remotePath;
    if (vitaPath.startsWith(QLatin1Char('/')))
        vitaPath.remove(0, 1);

    // A bare device root ("ux0:") does not stat on hardware -- sceIoGetstat
    // needs "ux0:/". libftpvita's LIST does not add that slash, so this is
    // exactly where a path handed to LIST silently stops resolving.
    static const QRegularExpression deviceRoot(QStringLiteral("^[a-z]+[0-9]*:$"));
    if (deviceRoot.match(vitaPath).hasMatch())
        return false;

    return QFileInfo::exists(m_root.filePath(vitaPath));
}

bool MockVitaServer::resolveForCwd(const QString &current, const QString &argument,
                                   QString *resolved) const
{
    if (argument == QLatin1String("/")) {
        *resolved = QStringLiteral("/");
        return true;
    }

    QString candidate;
    if (argument == QLatin1String("..")) {
        candidate = current;
        const int slash = candidate.lastIndexOf(QLatin1Char('/'));
        candidate = slash <= 0 ? QStringLiteral("/") : candidate.left(slash);
    } else if (argument.startsWith(QLatin1Char('/'))) {
        candidate = argument;
    } else {
        candidate = current.endsWith(QLatin1Char('/')) ? current + argument
                                                       : current + QLatin1Char('/') + argument;
    }

    // The fixup LIST lacks: "/ux0:" becomes "/ux0:/" so it opens as a
    // directory. This is why navigating with CWD works where LIST does not.
    if (candidate.lastIndexOf(QLatin1Char('/')) == 0 && !candidate.endsWith(QLatin1Char('/')))
        candidate += QLatin1Char('/');

    if (candidate != QLatin1String("/")) {
        QString vitaPath = candidate.mid(1);
        if (vitaPath.endsWith(QLatin1Char('/')))
            vitaPath.chop(1);
        if (!QFileInfo(m_root.filePath(vitaPath)).isDir())
            return false;
    }

    *resolved = candidate;
    return true;
}

QByteArray MockVitaServer::buildDeviceListing() const
{
    // What a real Vita answers for "/": its mount points, whether or not they
    // are backed by anything here.
    static const QStringList devices = {
        QStringLiteral("os0:"),  QStringLiteral("pd0:"),  QStringLiteral("sa0:"),
        QStringLiteral("tm0:"),  QStringLiteral("ud0:"),  QStringLiteral("uma0:"),
        QStringLiteral("ur0:"),  QStringLiteral("ux0:"),  QStringLiteral("vd0:"),
        QStringLiteral("vs0:")
    };

    QByteArray out;
    for (const QString &device : devices) {
        out += QStringLiteral("drwxrwxrwx 1 vita vita 0 Jan 01 1970 %1\r\n")
                   .arg(device)
                   .toUtf8();
    }
    return out;
}

QByteArray MockVitaServer::buildListing(const QString &localDir) const
{
    QByteArray out;
    const QFileInfoList entries = QDir(localDir).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &info : entries) {
        out += QStringLiteral("%1 1 vita vita %2 %3 %4\r\n")
                   .arg(info.isDir() ? QStringLiteral("drwxrwxrwx") : QStringLiteral("-rw-rw-rw-"))
                   .arg(info.isDir() ? 0 : info.size())
                   .arg(info.lastModified().toString(QStringLiteral("MMM dd hh:mm")))
                   .arg(info.fileName())
                   .toUtf8();
    }
    return out;
}

bool MockVitaServer::openPassivePort(QTcpSocket *control)
{
    Session *session = m_sessions.value(control);
    if (!session)
        return false;

    if (session->passive) {
        session->passive->deleteLater();
        session->passive = nullptr;
    }

    session->dataClosed = false;
    session->incoming.clear();

    session->passive = new QTcpServer(this);
    if (!session->passive->listen(QHostAddress::LocalHost, 0))
        return false;

    connect(session->passive, &QTcpServer::newConnection, this, [this, control, session] {
        session->data = session->passive->nextPendingConnection();
        if (!session->data)
            return;

        // Buffer unconditionally. A client may finish writing and close the
        // data socket before this server has read the command that explains
        // what the bytes were for, and those bytes still have to be kept.
        connect(session->data, &QTcpSocket::readyRead, this, [session] {
            session->incoming.append(session->data->readAll());
        });

        connect(session->data, &QTcpSocket::disconnected, this, [this, control, session] {
            session->dataClosed = true;
            if (session->receiving)
                finishUpload(control);
            // The socket is kept until its command has been serviced, so a
            // connection that closed early is not silently forgotten.
            // finishUpload may already have released it.
            if (session->data && !session->receiving && session->pendingCommand.isEmpty()) {
                session->data->deleteLater();
                session->data = nullptr;
            }
        });

        startDataTransfer(control);
    });

    const quint16 dataPort = session->passive->serverPort();
    reply(control, 227,
          QStringLiteral("Entering Passive Mode (127,0,0,1,%1,%2)")
              .arg(dataPort >> 8)
              .arg(dataPort & 0xff));
    return true;
}

void MockVitaServer::startDataTransfer(QTcpSocket *control)
{
    Session *session = m_sessions.value(control);
    if (!session || !session->data)
        return;

    // A client may connect the data socket either before or after it sends
    // the transfer command; a real server copes with both, so this one waits
    // rather than hanging up on the early-connect case.
    if (session->pendingCommand.isEmpty())
        return;

    const QString command = session->pendingCommand;
    const QString argument = session->pendingArgument;
    session->pendingCommand.clear();

    if (command == QLatin1String("LIST")) {
        ++m_listCount;

        // libftpvita's cmd_LIST_func: the argument is used only if it stats,
        // and otherwise the request silently degrades to the working
        // directory. Reproducing that fallback rather than resolving the
        // argument faithfully is the point -- a mock that always honours the
        // path cannot catch a client that relies on it.
        QString target = session->currentPath;
        if (!argument.isEmpty() && vitaPathStats(argument))
            target = argument;

        reply(control, 150, QStringLiteral("Opening ASCII mode data transfer for LIST."));
        if (target == QLatin1String("/")) {
            session->data->write(buildDeviceListing());
        } else {
            QString vitaPath = target.mid(1);
            if (vitaPath.endsWith(QLatin1Char('/')))
                vitaPath.chop(1);
            session->data->write(buildListing(m_root.filePath(vitaPath)));
        }
        session->data->flush();
        session->data->disconnectFromHost();
        reply(control, 226, QStringLiteral("Directory send OK"));
        return;
    }

    if (command == QLatin1String("RETR")) {
        QFile file(resolve(argument));
        if (!file.open(QIODevice::ReadOnly)) {
            reply(control, 550, QStringLiteral("No such file"));
            session->data->disconnectFromHost();
            return;
        }
        reply(control, 150, QStringLiteral("Opening data connection"));
        session->data->write(file.readAll());
        session->data->flush();
        session->data->disconnectFromHost();
        reply(control, 226, QStringLiteral("Transfer complete"));
        return;
    }

    if (command == QLatin1String("STOR")) {
        session->receiving = true;
        session->incomingPath = resolve(argument);
        QDir().mkpath(QFileInfo(session->incomingPath).absolutePath());
        reply(control, 150, QStringLiteral("Ok to send data"));
        // The client may already have delivered everything and hung up.
        if (session->dataClosed)
            finishUpload(control);
        return;
    }

    session->data->disconnectFromHost();
}

void MockVitaServer::finishUpload(QTcpSocket *control)
{
    Session *session = m_sessions.value(control);
    if (!session || !session->receiving)
        return;

    QFile file(session->incomingPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(session->incoming);

    session->receiving = false;
    session->incoming.clear();
    reply(control, 226, QStringLiteral("Transfer complete"));

    if (session->data) {
        session->data->deleteLater();
        session->data = nullptr;
    }

    ++m_completedUploads;
    if (m_dropAfterUploads > 0 && m_completedUploads == m_dropAfterUploads) {
        // Hang up without warning, exactly as a device that has run out of
        // sockets or memory does. Deferred by one turn of the event loop:
        // aborting here would tear the session down underneath the handler
        // that is still using it.
        QTimer::singleShot(0, this, [control] { control->abort(); });
    }
}

void MockVitaServer::handleLine(QTcpSocket *control, const QString &line)
{
    Session *session = m_sessions.value(control);
    if (!session)
        return;

    const int space = line.indexOf(QLatin1Char(' '));
    const QString verb = (space > 0 ? line.left(space) : line).toUpper();
    const QString argument = space > 0 ? line.mid(space + 1) : QString();

    m_commandLog.append(line);

    if (verb == QLatin1String("USER")) {
        reply(control, 230, QStringLiteral("User logged in"));
    } else if (verb == QLatin1String("PASS")) {
        reply(control, 230, QStringLiteral("User logged in"));
    } else if (verb == QLatin1String("CWD")) {
        QString resolved;
        if (argument.isEmpty()) {
            reply(control, 500, QStringLiteral("Syntax error, command unrecognized."));
        } else if (resolveForCwd(session->currentPath, argument, &resolved)) {
            session->currentPath = resolved;
            reply(control, 250, QStringLiteral("Requested file action okay, completed."));
        } else {
            reply(control, 550, QStringLiteral("Invalid directory."));
        }
    } else if (verb == QLatin1String("CDUP")) {
        QString resolved;
        if (resolveForCwd(session->currentPath, QStringLiteral(".."), &resolved)) {
            session->currentPath = resolved;
            reply(control, 250, QStringLiteral("Requested file action okay, completed."));
        } else {
            reply(control, 550, QStringLiteral("Invalid directory."));
        }
    } else if (verb == QLatin1String("PWD")) {
        reply(control, 257, QStringLiteral("\"%1\"").arg(session->currentPath));
    } else if (verb == QLatin1String("SYST")) {
        reply(control, 215, QStringLiteral("UNIX Type: L8"));
    } else if (verb == QLatin1String("TYPE")) {
        reply(control, 200, QStringLiteral("Type set"));
    } else if (verb == QLatin1String("NOOP")) {
        reply(control, 200, QStringLiteral("NOOP ok"));
    } else if (verb == QLatin1String("PASV")) {
        if (!openPassivePort(control))
            reply(control, 425, QStringLiteral("Cannot open passive port"));
    } else if (verb == QLatin1String("LIST") || verb == QLatin1String("RETR")
               || verb == QLatin1String("STOR")) {
        session->pendingCommand = verb;
        session->pendingArgument = argument;
        if (session->data)
            startDataTransfer(control);
    } else if (verb == QLatin1String("SIZE")) {
        const QFileInfo info(resolve(argument));
        if (!info.exists())
            reply(control, 550, QStringLiteral("No such file"));
        else
            reply(control, 213, QString::number(m_corruptSize ? info.size() - 1 : info.size()));
    } else if (verb == QLatin1String("MKD")) {
        const bool made = QDir().mkpath(resolve(argument));
        reply(control, made ? 257 : 550, argument);
    } else if (verb == QLatin1String("DELE")) {
        reply(control, QFile::remove(resolve(argument)) ? 250 : 550, QStringLiteral("DELE"));
    } else if (verb == QLatin1String("RMD")) {
        reply(control, QDir(resolve(argument)).removeRecursively() ? 250 : 550, QStringLiteral("RMD"));
    } else if (verb == QLatin1String("RNFR")) {
        session->renameFrom = resolve(argument);
        reply(control, 350, QStringLiteral("Ready for destination"));
    } else if (verb == QLatin1String("RNTO")) {
        const bool ok = QFile::rename(session->renameFrom, resolve(argument));
        reply(control, ok ? 250 : 550, QStringLiteral("RNTO"));
    } else if (verb == QLatin1String("QUIT")) {
        reply(control, 221, QStringLiteral("Goodbye"));
        control->disconnectFromHost();
    } else {
        reply(control, 502, QStringLiteral("Command not implemented"));
    }
}
