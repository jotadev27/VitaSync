// A stand-in PS Vita on localhost.
//
// Serves a real directory over the same narrow FTP dialect VitaShell speaks, so
// the desktop client can be exercised end to end -- connect, browse, install,
// download -- without a console on the desk. Development tool, not shipped.

#include "MockVitaServer.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>

#include <cstdio>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("mockvita"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Serves a directory as if it were a PS Vita running VitaShell's FTP."));
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("root"),
                                 QStringLiteral("Directory to serve as the device."));
    const QCommandLineOption portOption(
        QStringLiteral("port"), QStringLiteral("Port to listen on (0 picks a free one)."),
        QStringLiteral("n"), QStringLiteral("0"));
    const QCommandLineOption corruptOption(
        QStringLiteral("corrupt-size"),
        QStringLiteral("Report wrong sizes, to exercise the client's verification."));
    const QCommandLineOption dropOption(
        QStringLiteral("drop-after-uploads"),
        QStringLiteral("Hang up after <n> completed uploads, like a device out of resources."),
        QStringLiteral("n"), QStringLiteral("0"));
    parser.addOption(portOption);
    parser.addOption(corruptOption);
    parser.addOption(dropOption);
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(1);
        return 1;
    }

    const QString root = QDir(positional.first()).absolutePath();
    if (!QDir(root).exists()) {
        fprintf(stderr, "No such directory: %s\n", qPrintable(root));
        return 1;
    }

    MockVitaServer server(root);
    server.setCorruptSizeReports(parser.isSet(corruptOption));
    server.setDropControlAfterUploads(parser.value(dropOption).toInt());
    if (!server.listen(static_cast<quint16>(parser.value(portOption).toUShort()))) {
        fprintf(stderr, "Could not listen\n");
        return 1;
    }

    // Printed on one line so a script can read the port back.
    printf("mockvita listening 127.0.0.1:%u root=%s\n", server.port(), qPrintable(root));
    fflush(stdout);

    return app.exec();
}
