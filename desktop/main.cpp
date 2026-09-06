// VitaSync -- desktop entry point.
//
// This file does three things and nothing else: it registers the core types
// with QML, it hands the UI a single VitaDevice, and it starts the engine.
// All behaviour lives in vsp_core so the Android package can reuse it verbatim.

#include <vsp/DropStageModel.h>
#include <vsp/MetadataDb.h>
#include <vsp/RemoteBrowserModel.h>
#include <vsp/TransferQueue.h>
#include <vsp/Version.h>
#include <vsp/VitaDevice.h>

#include <Thumbnailer.h>

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStandardPaths>

#include <cstdio>
#include <QTimer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("VitaSync"));
    QCoreApplication::setOrganizationName(QStringLiteral("VitaSync"));
    QCoreApplication::setApplicationVersion(vsp::versionString());
    QGuiApplication::setApplicationDisplayName(vsp::applicationName());
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/VitaSync/Ui/brand/appicon.png")));

    // Basic style: every control in this app is drawn by hand, so the built-in
    // styling never gets a chance to impose its own look.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Headless capture, for documentation and for checking the layout in CI
    // without a display attached. Off unless asked for.
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Local-network companion for a jailbroken PS Vita"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Render the window to <file> and exit."),
        QStringLiteral("file"));
    // Connecting and sending from the command line makes the app scriptable:
    //   vitasync --connect 192.168.0.40 --send game.vpk theme.zip
    const QCommandLineOption connectOption(
        QStringLiteral("connect"),
        QStringLiteral("Connect to <host[:port]> on start."),
        QStringLiteral("host"));
    const QCommandLineOption sendOption(
        QStringLiteral("send"),
        QStringLiteral("Send the staged files as soon as the device answers."));
    const QCommandLineOption screenshotDelayOption(
        QStringLiteral("screenshot-delay"),
        QStringLiteral("Milliseconds to let the app settle before capturing."),
        QStringLiteral("ms"), QStringLiteral("1200"));
    const QCommandLineOption pathOption(
        QStringLiteral("path"),
        QStringLiteral("Open the browser at <remote-path> once connected."),
        QStringLiteral("remote-path"));
    const QCommandLineOption logOption(
        QStringLiteral("log"),
        QStringLiteral("Mirror the protocol trace to standard error."));
    const QCommandLineOption viewOption(
        QStringLiteral("view"),
        QStringLiteral("Screen shown on start: link, install, browse or transfers."),
        QStringLiteral("name"), QStringLiteral("link"));
    parser.addOption(screenshotOption);
    parser.addOption(screenshotDelayOption);
    parser.addOption(viewOption);
    parser.addOption(connectOption);
    parser.addOption(sendOption);
    parser.addOption(pathOption);
    parser.addOption(logOption);
    // Files named on the command line are staged on start, so the app works
    // as a drop target on the desktop and as an "open with" handler.
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Packages or media to stage on start."),
                                 QStringLiteral("[files...]"));
    parser.process(app);

    qmlRegisterUncreatableType<vsp::RemoteBrowserModel>(
        "VitaSync", 1, 0, "RemoteBrowserModel",
        QStringLiteral("Provided by Device.browser"));
    qmlRegisterUncreatableType<vsp::TransferQueue>(
        "VitaSync", 1, 0, "TransferQueue",
        QStringLiteral("Provided by Device.transfers"));
    qmlRegisterUncreatableType<vsp::DropStageModel>(
        "VitaSync", 1, 0, "DropStageModel",
        QStringLiteral("Provided by Device.drops"));
    qmlRegisterUncreatableType<vsp::MetadataDb>(
        "VitaSync", 1, 0, "MetadataDb",
        QStringLiteral("Provided by Device.metadata"));

    auto *device = new vsp::VitaDevice(&app);

    // Previews for staged files: embedded artwork, a photo scaled down, a
    // decoded video frame, or a plate drawn from a theme's own colours. All of
    // it local, all of it from the file that was dropped.
    auto *thumbnailer = new vsp::Thumbnailer(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/thumbnails"),
        &app);
    device->drops()->setThumbnailProvider(thumbnailer);
    const QStringList staged = parser.positionalArguments();

    if (parser.isSet(logOption)) {
        QObject::connect(device, &vsp::VitaDevice::logLineAdded, device,
                         [](const QString &line) {
                             fprintf(stderr, "%s\n", qPrintable(line));
                         });
        QObject::connect(device, &vsp::VitaDevice::notify, device,
                         [](const QString &message, const QString &tone) {
                             fprintf(stderr, "[%s] %s\n", qPrintable(tone), qPrintable(message));
                         });
    }
    qmlRegisterSingletonInstance("VitaSync", 1, 0, "Device", device);

    QQmlApplicationEngine engine;
    // Staging files opens the Install screen, since that is what the user just
    // asked to look at -- unless they named a screen explicitly, in which case
    // the flag they typed wins.
    const QString startView = parser.isSet(viewOption) || staged.isEmpty()
                                  ? parser.value(viewOption)
                                  : QStringLiteral("install");
    engine.setInitialProperties({ { QStringLiteral("startView"), startView } });
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("VitaSync.Ui", "Main");

    if (!staged.isEmpty())
        device->addDrops(staged);

    if (parser.isSet(connectOption)) {
        const QString target = parser.value(connectOption);
        const int colon = target.lastIndexOf(QLatin1Char(':'));
        device->setHost(colon > 0 ? target.left(colon) : target);
        if (colon > 0) {
            bool ok = false;
            const int port = target.mid(colon + 1).toInt(&ok);
            if (ok)
                device->setPort(port);
        }

        if (parser.isSet(pathOption)) {
            const QString target = parser.value(pathOption);
            QObject::connect(device, &vsp::VitaDevice::statusChanged, device, [device, target] {
                static bool navigated = false;
                if (navigated || !device->isConnected())
                    return;
                navigated = true;
                // Connecting already queues a listing of the card. Defer by one
                // turn of the event loop so this replaces it rather than
                // racing it.
                QTimer::singleShot(0, device, [device, target] {
                    device->navigateTo(target);
                });
            });
        }

        if (parser.isSet(sendOption)) {
            // Wait for the device to actually answer before sending; a failed
            // connection must not silently look like a completed send.
            QObject::connect(device, &vsp::VitaDevice::statusChanged, device, [device] {
                static bool sent = false;
                if (!sent && device->isConnected() && device->drops()->rowCount() > 0) {
                    sent = true;
                    device->startStaged();
                }
            });
        }
        device->connectToVita();
    }

    if (parser.isSet(screenshotOption)) {
        const QString target = parser.value(screenshotOption);
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
        if (!window)
            return 1;
        // One frame is not enough: asynchronous images and the layout pass
        // both need to settle before the grab is representative.
        bool delayOk = false;
        const int delay = parser.value(screenshotDelayOption).toInt(&delayOk);
        QTimer::singleShot(delayOk ? qBound(0, delay, 600000) : 1200, window,
                           [window, &app, target] {
            const QImage frame = window->grabWindow();
            app.exit(!frame.isNull() && frame.save(target) ? 0 : 1);
        });
    }

    return app.exec();
}
