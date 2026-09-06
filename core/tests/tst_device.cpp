// Integration tests for VitaDevice -- the object the UI actually binds to.
//
// The transport tests prove the queue and the FTP client work. These prove the
// wiring between them does: that a drop is identified, routed to the right
// folder, sent, and reported, exactly as pressing Start in the app would.

#include "MockVitaServer.h"
#include "TestArchives.h"

#include <vsp/DropStageModel.h>
#include <vsp/RemoteBrowserModel.h>
#include <vsp/TransferQueue.h>
#include <vsp/VitaDevice.h>
#include <vsp/VitaPaths.h>

#include <QDir>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace vsp;
using namespace vsptest;

class DeviceTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_deviceRoot;
    QTemporaryDir m_localRoot;
    QTemporaryDir m_downloadRoot;
    QTemporaryDir m_usbRoot;
    MockVitaServer *m_server = nullptr;

    QString devicePath(const QString &relative) const
    {
        return m_deviceRoot.path() + QLatin1Char('/') + relative;
    }

    /// Unlike devicePath(), no "ux0:" segment: a mounted USB volume's own
    /// root *is* ux0:'s root -- there is no further folder inside it. See
    /// UsbTransport::mapToLocal() and docs/usb.md.
    QString usbPath(const QString &relative) const
    {
        return m_usbRoot.path() + QLatin1Char('/') + relative;
    }

    /// The names the browser is actually showing, in order.
    static QStringList listedNames(VitaDevice *device)
    {
        QStringList names;
        for (int row = 0; row < device->browser()->rowCount(); ++row)
            names << device->browser()->itemAt(row).value(QStringLiteral("name")).toString();
        return names;
    }

    /// Brings a device up and connected, or fails the test.
    bool connectDevice(VitaDevice *device)
    {
        QSignalSpy statusSpy(device, &VitaDevice::statusChanged);
        device->setHost(QStringLiteral("127.0.0.1"));
        device->setPort(m_server->port());
        device->connectToVita();

        const int deadline = 5000;
        QElapsedTimer timer;
        timer.start();
        while (!device->isConnected() && timer.elapsed() < deadline)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        return device->isConnected();
    }

    /// Same idea as connectDevice(), but over the USB transport -- pointed at
    /// a plain folder standing in for a mounted Vita card, since there is no
    /// protocol to dial into.
    bool connectDeviceUsb(VitaDevice *device)
    {
        device->setConnectionMode(QStringLiteral("usb"));
        device->setUsbRootPath(m_usbRoot.path());
        device->connectUsb();
        return device->isConnected();
    }

private slots:
    void initTestCase()
    {
        // Keep QSettings out of the developer's real configuration.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("VitaSyncTest"));
        QCoreApplication::setApplicationName(QStringLiteral("VitaSyncTest"));

        QVERIFY(m_deviceRoot.isValid());
        QVERIFY(m_localRoot.isValid());
        QVERIFY(m_downloadRoot.isValid());
        QVERIFY(m_usbRoot.isValid());

        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/vpk"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/video"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/customtheme"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/VitaShell/theme"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/user/00/savedata/PCSE00001/sub"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/picture"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/music"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("os0:"));

        QFile clip(devicePath(QStringLiteral("ux0:/video/clip.mp4")));
        QVERIFY(clip.open(QIODevice::WriteOnly));
        clip.write(QByteArray(256, 'V'));
        clip.close();

        QFile pkg(devicePath(QStringLiteral("ux0:/vpk/already.vpk")));
        QVERIFY(pkg.open(QIODevice::WriteOnly));
        pkg.write(QByteArray(128, 'P'));
        pkg.close();

        QFile save(devicePath(QStringLiteral("ux0:/user/00/savedata/PCSE00001/data.bin")));
        QVERIFY(save.open(QIODevice::WriteOnly));
        save.write(QByteArray(2048, 'S'));
        save.close();

        QFile nested(devicePath(QStringLiteral("ux0:/user/00/savedata/PCSE00001/sub/n.bin")));
        QVERIFY(nested.open(QIODevice::WriteOnly));
        nested.write(QByteArray(512, 'N'));
        nested.close();

        m_server = new MockVitaServer(m_deviceRoot.path(), this);
        QVERIFY(m_server->listen());

        // A USB-mounted volume standing in for a Vita card: the Vita
        // signature (id.dat + a VitaShell install folder) plus the same
        // top-level layout the FTP fixture has, minus the "ux0:" segment --
        // see usbPath().
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("vpk"));
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("video"));
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("customtheme"));
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("VitaShell/theme"));
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("app/VITASHELL"));
        QDir(m_usbRoot.path()).mkpath(QStringLiteral("user/00/savedata/PCSE00001"));

        QFile id(usbPath(QStringLiteral("id.dat")));
        QVERIFY(id.open(QIODevice::WriteOnly));
        id.write(QByteArray(32, '\0'));
        id.close();

        QFile usbPkg(usbPath(QStringLiteral("vpk/already.vpk")));
        QVERIFY(usbPkg.open(QIODevice::WriteOnly));
        usbPkg.write(QByteArray(128, 'P'));
        usbPkg.close();
    }

    void connectsAndListsTheCard()
    {
        VitaDevice device;
        QVERIFY2(connectDevice(&device), "device never connected");

        // Connecting navigates to the card and lists it without being asked.
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);
        QCOMPARE(device.currentPath(), QStringLiteral("/ux0:"));
        QCOMPARE(device.statusTone(), QStringLiteral("ok"));
    }

    void recentHostsRememberThePortAndCanBeCleared()
    {
        // A successful connection is remembered as one host:port pair, not
        // just the bare address -- a Vita on a non-default FTP port used to
        // lose that the moment it left the field.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString expected =
            QStringLiteral("127.0.0.1:%1").arg(m_server->port());
        QVERIFY2(device.recentHosts().contains(expected),
                 qPrintable(device.recentHosts().join(", ")));

        device.setHost(QStringLiteral("10.0.0.1"));
        device.setPort(2121);
        device.selectRecentHost(expected);
        QCOMPARE(device.host(), QStringLiteral("127.0.0.1"));
        QCOMPARE(device.port(), m_server->port());

        // A malformed entry (no port, or a non-numeric one) changes nothing
        // rather than leaving host and port out of sync with each other.
        device.setHost(QStringLiteral("10.0.0.1"));
        device.setPort(2121);
        device.selectRecentHost(QStringLiteral("no-port-here"));
        QCOMPARE(device.host(), QStringLiteral("10.0.0.1"));
        QCOMPARE(device.port(), 2121);

        device.clearRecentHosts();
        QVERIFY(device.recentHosts().isEmpty());
    }

    // --- USB mode ---------------------------------------------------------
    // Same object, same TransferQueue, same install pipeline -- only the
    // transport underneath differs. These mirror the FTP tests above closely
    // on purpose: the point is that nothing in VitaDevice forked per mode.

    void usbModeConnectsAndListsTheCard()
    {
        VitaDevice device;
        QVERIFY2(connectDeviceUsb(&device), "device never connected over USB");

        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);
        QCOMPARE(device.currentPath(), QStringLiteral("/ux0:"));
        QCOMPARE(device.statusText(), QStringLiteral("USB Connected"));
        QCOMPARE(device.statusTone(), QStringLiteral("ok"));
        QVERIFY(listedNames(&device).contains(QStringLiteral("vpk")));

        device.disconnectFromVita();
        QVERIFY(!device.isConnected());
        QCOMPARE(device.statusText(), QStringLiteral("USB Disconnected"));
    }

    void usbModeNeverShowsAHostAndPortLikeAFtpSession()
    {
        // Real-hardware finding: the top bar showed something shaped like
        // "127.0.0.1:2202" while connected over USB, which is not a socket
        // and never was one -- USB is a mounted folder (see docs/usb.md).
        // TopBar.qml binds to these two properties instead of raw
        // host/port; pin what they actually contain for each mode.
        VitaDevice device;

        QVERIFY(connectDevice(&device));
        QCOMPARE(device.connectionSummary(), QStringLiteral("127.0.0.1"));
        QCOMPARE(device.connectionDetail(), QStringLiteral(":%1").arg(m_server->port()));
        device.disconnectFromVita();

        QVERIFY2(connectDeviceUsb(&device), "device never connected over USB");
        QCOMPARE(device.connectionDetail(), QStringLiteral("USB"));
        // The volume's own name, not a network address.
        QCOMPARE(device.connectionSummary(), QFileInfo(m_usbRoot.path()).fileName());
        QVERIFY2(!device.connectionSummary().contains(QLatin1Char(':')),
                 qPrintable(QStringLiteral("USB summary looked like a host:port address: %1")
                                .arg(device.connectionSummary())));
    }

    void usbRootFailsClearlyInsteadOfHangingOrFalselyDisconnecting()
    {
        // Real-hardware finding: navigating to Root over USB "felt like it
        // hung or disconnected" and stayed stuck on ux0:. Only ux0: is ever
        // reachable over USB mass storage (see docs/usb.md) -- Root asking
        // for the true device root is expected to fail -- but it must fail
        // fast, with a clear reason, and without tearing down the
        // connection the way a real device-loss would.
        VitaDevice device;
        QVERIFY2(connectDeviceUsb(&device), "device never connected over USB");
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        const QString rootPath = quickLocationPath(&device, QStringLiteral("root"));
        QVERIFY2(!rootPath.isEmpty(), "no 'root' quick location found");

        device.navigateTo(rootPath);
        // Busy, then Ready, then the error land as separate statusChanged
        // emissions in sequence; wait for the one that actually matters.
        QTRY_VERIFY_WITH_TIMEOUT(device.statusTone() == QStringLiteral("error"), 2000);

        // A clear reason, not silence.
        QVERIFY2(device.statusText().contains(QLatin1String("ux0:")),
                 qPrintable(device.statusText()));
        QCOMPARE(device.statusTone(), QStringLiteral("error"));
        // Still linked -- this was a request USB can never satisfy, not the
        // device going away, and the two must not be confused.
        QVERIFY(device.isConnected());
        QCOMPARE(device.connectionDetail(), QStringLiteral("USB"));
    }

    void usbModeSendsAGameToTheVpkFolder()
    {
        VitaDevice device;
        QVERIFY2(connectDeviceUsb(&device), "device never connected over USB");

        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Gravity Rush") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSA00011") }
        });
        const QString archive = m_localRoot.filePath(QStringLiteral("Gravity Rush USB.vpk"));
        QVERIFY(!writeArchive(archive, {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("eboot.bin"), QByteArray(8192, 'E'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ archive }), 1);

        QSignalSpy readySpy(&device, &VitaDevice::installReady);
        device.startStaged();
        QVERIFY2(readySpy.wait(20000), "the game never reached install-ready over USB");

        QCOMPARE(readySpy.first().at(2).toString(), QStringLiteral("/ux0:/vpk/Gravity Rush USB.vpk"));
        QVERIFY(QFileInfo::exists(usbPath(QStringLiteral("vpk/Gravity Rush USB.vpk"))));
    }

    void switchingModeDropsTheOldLinkFirst()
    {
        VitaDevice device;
        QVERIFY2(connectDevice(&device), "device never connected over FTP");
        QVERIFY(device.isConnected());

        device.setConnectionMode(QStringLiteral("usb"));
        // Wi-Fi/FTP is not touched merely by picking the tab -- it only drops
        // if a link was actually live, which it was here.
        QVERIFY(!device.isConnected());
        QCOMPARE(device.connectionMode(), QStringLiteral("usb"));

        QVERIFY2(connectDeviceUsb(&device), "device never connected over USB after switching");
        device.disconnectFromVita();
    }

    void refreshDoesNotRunAway()
    {
        // A refresh must issue exactly one listing. It used to register itself
        // as a pending refresh, which made every completed listing schedule
        // another one and flooded the connection.
        VitaDevice device;
        QVERIFY(connectDevice(&device));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        const int before = m_server->listCount();
        device.refresh();

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 1500)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 25);

        const int issued = m_server->listCount() - before;
        QVERIFY2(issued <= 2,
                 qPrintable(QStringLiteral("one refresh issued %1 listings").arg(issued)));
    }

    void dropAndStartSendsAGameToTheVpkFolder()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Gravity Rush") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSA00011") }
        });
        const QString archive = m_localRoot.filePath(QStringLiteral("Gravity Rush.vpk"));
        QVERIFY(!writeArchive(archive, {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("eboot.bin"), QByteArray(8192, 'E'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ archive }), 1);
        QCOMPARE(device.drops()->rowCount(), 1);

        QSignalSpy readySpy(&device, &VitaDevice::installReady);
        device.startStaged();
        QVERIFY2(readySpy.wait(20000), "the game never reached install-ready");

        QCOMPARE(readySpy.first().at(2).toString(), QStringLiteral("/ux0:/vpk/Gravity Rush.vpk"));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/vpk/Gravity Rush.vpk"))));
        // Starting clears the staging area, so nothing is sent twice.
        QCOMPARE(device.drops()->rowCount(), 0);
    }

    void dropAndStartSendsASystemThemeFolder()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString archive = m_localRoot.filePath(QStringLiteral("Neon Drift.zip"));
        QVERIFY(!writeArchive(archive, {
            { QStringLiteral("Neon_Drift/theme.xml"),
              makeThemeXml(QStringLiteral("Neon Drift"), QStringLiteral("kaido")), true },
            { QStringLiteral("Neon_Drift/br.png"), QByteArray(4096, 'B'), true },
            { QStringLiteral("Neon_Drift/icons/settings.png"), QByteArray(256, 'S'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ archive }), 1);

        // The staging row must show where it is going before anything is sent.
        const QModelIndex row = device.drops()->index(0, 0);
        QCOMPARE(device.drops()->data(row, DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/customtheme"));
        QCOMPARE(device.drops()->data(row, DropStageModel::TitleRole).toString(),
                 QStringLiteral("Neon Drift"));

        QSignalSpy themeSpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY2(themeSpy.wait(25000), "the theme never finished transferring");

        QCOMPARE(themeSpy.first().at(1).toString(),
                 QStringLiteral("/ux0:/customtheme/Neon_Drift"));
        QVERIFY(themeSpy.first().at(2).toString().contains(QLatin1String("Custom Themes Manager")));

        const QString root = devicePath(QStringLiteral("ux0:/customtheme/Neon_Drift"));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/theme.xml")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/br.png")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/icons/settings.png")));
    }

    void dropAndStartSendsAShellThemeToVitaShell()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString archive = m_localRoot.filePath(QStringLiteral("Midnight.zip"));
        QVERIFY(!writeArchive(archive, {
            { QStringLiteral("Midnight/colors.txt"), QByteArray("BG = 0x0\n"), false },
            { QStringLiteral("Midnight/wallpaper.png"), QByteArray(2048, 'W'), true },
            { QStringLiteral("Midnight/folder_icon.png"), QByteArray(128, 'F'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ archive }), 1);
        QCOMPARE(device.drops()->data(device.drops()->index(0, 0),
                                     DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/VitaShell/theme"));

        QSignalSpy themeSpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY2(themeSpy.wait(25000), "the shell theme never finished");

        QVERIFY(QFileInfo::exists(
            devicePath(QStringLiteral("ux0:/VitaShell/theme/Midnight/colors.txt"))));
    }

    void startingSeveralMixedDropsSendsThemAll()
    {
        // The app's real case: two themes and a game staged together and sent
        // in one press. Each is a different shape of job, and they have to
        // queue behind one another without tripping over each other.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString themeA = m_localRoot.filePath(QStringLiteral("Mixed Theme A.zip"));
        QVERIFY(!writeArchive(themeA, {
            { QStringLiteral("Theme_A/theme.xml"),
              makeThemeXml(QStringLiteral("Theme A"), QStringLiteral("kaido")), true },
            { QStringLiteral("Theme_A/br.png"), QByteArray(8192, 'A'), true },
            { QStringLiteral("Theme_A/icons/a.png"), QByteArray(256, 'a'), false }
        }).isEmpty());

        const QString themeB = m_localRoot.filePath(QStringLiteral("Mixed Shell B.zip"));
        QVERIFY(!writeArchive(themeB, {
            { QStringLiteral("Shell_B/colors.txt"), QByteArray("BG = 0x0\n"), false },
            { QStringLiteral("Shell_B/wallpaper.png"), QByteArray(4096, 'B'), true }
        }).isEmpty());

        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Mixed Game") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSA00099") }
        });
        const QString game = m_localRoot.filePath(QStringLiteral("Mixed Game.vpk"));
        QVERIFY(!writeArchive(game, {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("eboot.bin"), QByteArray(4096, 'E'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ themeA, themeB, game }), 3);

        QStringList failures;
        connect(device.transfers(), &TransferQueue::jobFinished, this,
                [&failures](int, bool ok, const QString &message) {
                    if (!ok)
                        failures << message;
                });

        device.startStaged();

        // Wait for the queue to drain rather than for one signal: three jobs
        // finish in three different ways.
        QTRY_VERIFY_WITH_TIMEOUT(!device.transfers()->isBusy()
                                     && device.transfers()->activeCount() == 0,
                                 40000);

        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(" | "))));

        QVERIFY(QFileInfo::exists(
            devicePath(QStringLiteral("ux0:/customtheme/Theme_A/theme.xml"))));
        QVERIFY(QFileInfo::exists(
            devicePath(QStringLiteral("ux0:/customtheme/Theme_A/icons/a.png"))));
        QVERIFY(QFileInfo::exists(
            devicePath(QStringLiteral("ux0:/VitaShell/theme/Shell_B/colors.txt"))));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/vpk/Mixed Game.vpk"))));
    }

    void everyKindOfDropLandsInItsOwnFolder_data()
    {
        // Only the picture case was confirmed by hand on real hardware. These
        // pin down the rest of the routing table so the others cannot drift.
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QString>("expectedDir");

        QTest::newRow("video mp4")  << "Holiday.mp4"  << "/ux0:/video";
        QTest::newRow("video mkv")  << "Movie.mkv"    << "/ux0:/video";
        QTest::newRow("photo jpg")  << "IMG_0042.jpg" << "/ux0:/picture";
        QTest::newRow("photo png")  << "Shot.png"     << "/ux0:/picture";
        QTest::newRow("music mp3")  << "Song.mp3"     << "/ux0:/music";
        QTest::newRow("music flac") << "Track.flac"   << "/ux0:/music";
    }

    void everyKindOfDropLandsInItsOwnFolder()
    {
        QFETCH(QString, fileName);
        QFETCH(QString, expectedDir);

        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString localPath = m_localRoot.filePath(fileName);
        QFile file(localPath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(4096, 'M'));
        file.close();

        QCOMPARE(device.addDrops({ localPath }), 1);
        // The destination is shown before Start, so this is what the user sees.
        QCOMPARE(device.drops()->data(device.drops()->index(0, 0),
                                     DropStageModel::DestinationRole).toString(),
                 expectedDir);

        QSignalSpy finishedSpy(device.transfers(), &TransferQueue::jobFinished);
        device.startStaged();
        QVERIFY(finishedSpy.wait(20000));
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));

        // And it really is there, on the card, in that folder.
        const QString landed = devicePath(expectedDir.mid(1) + QLatin1Char('/') + fileName);
        QVERIFY2(QFileInfo::exists(landed), qPrintable(landed));
    }

    void themeKindsLandInTheirOwnFolders_data()
    {
        QTest::addColumn<bool>("systemTheme");
        QTest::addColumn<QString>("expectedRoot");

        QTest::newRow("vita home-screen theme")
            << true << "/ux0:/customtheme/Routed_Theme";
        QTest::newRow("vitashell skin")
            << false << "/ux0:/VitaShell/theme/Routed_Theme";
    }

    void themeKindsLandInTheirOwnFolders()
    {
        QFETCH(bool, systemTheme);
        QFETCH(QString, expectedRoot);

        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString archive =
            m_localRoot.filePath(systemTheme ? QStringLiteral("routed-sys.zip")
                                             : QStringLiteral("routed-shell.zip"));
        if (systemTheme) {
            QVERIFY(!writeArchive(archive, {
                { QStringLiteral("Routed_Theme/theme.xml"),
                  makeThemeXml(QStringLiteral("Routed"), QStringLiteral("tester")), true },
                { QStringLiteral("Routed_Theme/br.png"), QByteArray(512, 'B'), false }
            }).isEmpty());
        } else {
            QVERIFY(!writeArchive(archive, {
                { QStringLiteral("Routed_Theme/colors.txt"), QByteArray("BG = 0x0\n"), false },
                { QStringLiteral("Routed_Theme/wallpaper.png"), QByteArray(512, 'W'), false }
            }).isEmpty());
        }

        QCOMPARE(device.addDrops({ archive }), 1);

        QSignalSpy themeSpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY2(themeSpy.wait(25000), "the theme never finished");
        QCOMPARE(themeSpy.first().at(1).toString(), expectedRoot);

        const QString marker = systemTheme ? QStringLiteral("theme.xml")
                                           : QStringLiteral("colors.txt");
        QVERIFY(QFileInfo::exists(devicePath(expectedRoot.mid(1) + QLatin1Char('/') + marker)));
    }

    void queuingFivePhotosCompletesAllOfThem()
    {
        // Mirrors a real-hardware report: five photos, one noticeably larger
        // than the rest, queued in one press. Checks the client survives a run
        // of back-to-back transfers rather than only a single one.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        QStringList staged;
        const QList<int> sizes = { 7900 * 1024, 1500 * 1024, 1200 * 1024,
                                   1400 * 1024, 1300 * 1024 };
        for (int i = 0; i < sizes.size(); ++i) {
            const QString name = QStringLiteral("photo-%1.jpg").arg(i);
            const QString localPath = m_localRoot.filePath(name);
            QFile file(localPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(sizes.at(i), static_cast<char>('A' + i)));
            file.close();
            staged << localPath;
        }

        QCOMPARE(device.addDrops(staged), 5);

        QStringList failures;
        connect(device.transfers(), &TransferQueue::jobFinished, this,
                [&failures](int, bool ok, const QString &message) {
                    if (!ok)
                        failures << message;
                });

        device.startStaged();
        QTRY_VERIFY_WITH_TIMEOUT(!device.transfers()->isBusy()
                                     && device.transfers()->activeCount() == 0,
                                 60000);

        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QStringLiteral(" | "))));
        QVERIFY2(device.isConnected(), "the connection did not survive the queue");

        for (int i = 0; i < sizes.size(); ++i) {
            const QString landed =
                devicePath(QStringLiteral("ux0:/picture/photo-%1.jpg").arg(i));
            QVERIFY2(QFileInfo::exists(landed), qPrintable(landed));
            QCOMPARE(QFileInfo(landed).size(), qint64(sizes.at(i)));
        }
    }

    void aDroppedConnectionPausesTheQueueRatherThanKillingIt()
    {
        // Real hardware dropped the control connection partway through a batch
        // of photos. libftpvita leaks its data connection when a per-transfer
        // allocation fails, so this can happen for reasons no client controls.
        // What the client owes the user is that the batch survives it.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        QStringList staged;
        for (int i = 0; i < 4; ++i) {
            const QString localPath =
                m_localRoot.filePath(QStringLiteral("resume-%1.jpg").arg(i));
            QFile file(localPath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(64 * 1024, static_cast<char>('a' + i)));
            file.close();
            staged << localPath;
        }
        QCOMPARE(device.addDrops(staged), 4);

        QSignalSpy postponedSpy(device.transfers(), &TransferQueue::jobPostponed);
        m_server->setDropControlAfterUploads(m_server->completedUploads() + 2);

        device.startStaged();

        // The queue must finish on its own: the reconnect is automatic and the
        // interrupted job goes back in the queue rather than being failed.
        QTRY_VERIFY_WITH_TIMEOUT(device.transfers()->failedCount() == 0
                                     && !device.transfers()->isBusy()
                                     && device.transfers()->activeCount() == 0,
                                 90000);
        m_server->setDropControlAfterUploads(0);

        QVERIFY2(postponedSpy.count() > 0,
                 "the interrupted job should have been postponed, not failed");
        QCOMPARE(device.transfers()->failedCount(), 0);

        for (int i = 0; i < 4; ++i) {
            const QString landed =
                devicePath(QStringLiteral("ux0:/picture/resume-%1.jpg").arg(i));
            QVERIFY2(QFileInfo::exists(landed), qPrintable(landed));
        }
    }

    void cancellingOneJobLeavesTheNextOneWorking()
    {
        // Reported from hardware: cancelling a package mid-transfer made the
        // next, untouched job fail immediately with "Device rejected binary
        // mode: Could not create the directory" -- the reply to a command two
        // steps earlier. Aborting left the server still owing replies for the
        // command in flight, and the next request read those instead of its
        // own.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        // A real package, big enough that the cancel genuinely lands mid-flight.
        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Cancel Me") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSE00777") }
        });
        const QString bigPath = m_localRoot.filePath(QStringLiteral("cancel-me.vpk"));
        QVERIFY(!writeArchive(bigPath, {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("eboot.bin"), QByteArray(24 * 1024 * 1024, 'B'), false }
        }).isEmpty());

        const QString smallPath = m_localRoot.filePath(QStringLiteral("after-cancel.mp4"));
        QFile small(smallPath);
        QVERIFY(small.open(QIODevice::WriteOnly));
        small.write(QByteArray(96 * 1024, 'S'));
        small.close();

        QCOMPARE(device.addDrops({ bigPath, smallPath }), 2);

        // Pull the plug on the first job the moment it is genuinely in flight.
        bool cancelled = false;
        int cancelledJobId = -1;
        connect(device.transfers(), &TransferQueue::dataChanged, this,
                [&](const QModelIndex &topLeft, const QModelIndex &, const QList<int> &) {
                    if (cancelled || topLeft.row() != 0)
                        return;
                    const qreal progress =
                        device.transfers()->data(topLeft, TransferQueue::ProgressRole).toReal();
                    if (progress <= 0.0 || progress >= 1.0)
                        return;
                    cancelled = true;
                    cancelledJobId =
                        device.transfers()->data(topLeft, TransferQueue::JobIdRole).toInt();
                    device.transfers()->cancelJob(cancelledJobId);
                });

        QString secondJobMessage;
        bool secondJobOk = false;
        bool secondJobDone = false;
        connect(device.transfers(), &TransferQueue::jobFinished, this,
                [&](int jobId, bool ok, const QString &message) {
                    if (jobId == cancelledJobId)
                        return;                    // the cancelled one
                    secondJobDone = true;
                    secondJobOk = ok;
                    secondJobMessage = message;
                });

        device.startStaged();
        QTRY_VERIFY_WITH_TIMEOUT(cancelled, 20000);
        QTRY_VERIFY_WITH_TIMEOUT(secondJobDone, 40000);

        QVERIFY2(secondJobOk, qPrintable(QStringLiteral(
            "the job after a cancel failed with: %1").arg(secondJobMessage)));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/video/after-cancel.mp4"))));
        QCOMPARE(QFileInfo(devicePath(QStringLiteral("ux0:/video/after-cancel.mp4"))).size(),
                 qint64(96 * 1024));
    }

    /// Builds an already-unpacked game on disk: sce_sys, an eboot, and a file
    /// in a subfolder so the tree really is a tree.
    QString makeGameFolder(const QString &folderName, const QString &titleId,
                           const QString &title, bool withSfo = true)
    {
        const QString root = m_localRoot.filePath(folderName);
        if (!QDir().mkpath(root + QStringLiteral("/sce_sys/livearea/contents"))
            || !QDir().mkpath(root + QStringLiteral("/module"))) {
            return {};
        }

        if (withSfo) {
            const QByteArray sfo = makeSfo({
                { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
                { QStringLiteral("TITLE"),    title },
                { QStringLiteral("TITLE_ID"), titleId },
                { QStringLiteral("APP_VER"),  QStringLiteral("01.00") }
            });
            QFile sfoFile(root + QStringLiteral("/sce_sys/param.sfo"));
            if (!sfoFile.open(QIODevice::WriteOnly))
                return {};
            sfoFile.write(sfo);
        }

        const QList<QPair<QString, QByteArray>> files = {
            { QStringLiteral("/eboot.bin"), QByteArray(4096, 'E') },
            { QStringLiteral("/sce_sys/icon0.png"), QByteArray(512, 'I') },
            { QStringLiteral("/sce_sys/livearea/contents/bg.png"), QByteArray(256, 'B') },
            { QStringLiteral("/module/libc.suprx"), QByteArray(128, 'M') }
        };
        for (const auto &entry : files) {
            QFile file(root + entry.first);
            if (!file.open(QIODevice::WriteOnly))
                return {};
            file.write(entry.second);
        }
        return root;
    }

    void anUnpackedGameFolderIsIdentifiedFromItsSfo()
    {
        const QString folder = makeGameFolder(QStringLiteral("Gravity Folder"),
                                              QStringLiteral("PCSA00011"),
                                              QStringLiteral("Gravity Rush"));
        QVERIFY(!folder.isEmpty());

        const PackageInfo info = PackageInspector::inspect(folder);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::FolderGame);
        // The Title ID comes from param.sfo, not from the folder's name.
        QCOMPARE(info.titleId, QStringLiteral("PCSA00011"));
        QCOMPARE(info.title, QStringLiteral("Gravity Rush"));
        QVERIFY(info.localIsDirectory);
        QVERIFY(info.isDirectoryPayload());
        QCOMPARE(info.entryCount, 5);
        QCOMPARE(info.iconPng.size(), 512);
    }

    void aFolderNamedLikeATitleIdIsAcceptedWithoutAnSfo()
    {
        const QString folder = makeGameFolder(QStringLiteral("PCSE00042"),
                                              QString(), QString(), false);
        QVERIFY(!folder.isEmpty());

        const PackageInfo info = PackageInspector::inspect(folder);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::FolderGame);
        QCOMPARE(info.titleId, QStringLiteral("PCSE00042"));
    }

    void aFolderThatIsNotAGameIsRefusedWithAReason()
    {
        const QString root = m_localRoot.filePath(QStringLiteral("Holiday Photos"));
        QVERIFY(QDir().mkpath(root));
        QFile file(root + QStringLiteral("/one.jpg"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(64, 'J'));
        file.close();

        const PackageInfo info = PackageInspector::inspect(root);
        QVERIFY(!info.valid);
        QVERIFY2(info.error.contains(QLatin1String("Title ID")), qPrintable(info.error));
    }

    void noNpDrmDumpIsRecognisedAndAGameFolderIsNot()
    {
        const QString dumpRoot = m_localRoot.filePath(QStringLiteral("NoNpDrm Signature Check"));
        QVERIFY(QDir().mkpath(dumpRoot + QStringLiteral("/app/PCSB00001")));
        QVERIFY(PackageInspector::looksLikeNoNpDrmDump(dumpRoot));

        // A normal unpacked game -- sce_sys directly inside, no app/addcont/
        // license subfolder -- must never be read as this format instead.
        const QString gameFolder = makeGameFolder(QStringLiteral("Not A NoNpDrm Dump"),
                                                  QStringLiteral("PCSE00099"),
                                                  QStringLiteral("Some Game"));
        QVERIFY(!gameFolder.isEmpty());
        QVERIFY(!PackageInspector::looksLikeNoNpDrmDump(gameFolder));
    }

    void noNpDrmDumpSplitsAcrossFixedDestinations()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        // app/PCSB00550: a full unpacked game, exactly the shape a standalone
        // FolderGame drop has -- reused via PackageInspector::inspectFolder().
        const QString appFolder = makeGameFolder(QStringLiteral("Astro NoNpDrm/app/PCSB00550"),
                                                  QStringLiteral("PCSB00550"),
                                                  QStringLiteral("Astro Bot"));
        QVERIFY(!appFolder.isEmpty());

        // addcont/PCSB00550: no param.sfo of its own, named by serial like an
        // unpacked game folder without an SFO.
        const QString addcontFolder =
            m_localRoot.filePath(QStringLiteral("Astro NoNpDrm/addcont/PCSB00550"));
        QVERIFY(QDir().mkpath(addcontFolder + QStringLiteral("/subdir")));
        QFile dlcFile(addcontFolder + QStringLiteral("/subdir/dlc_data.bin"));
        QVERIFY(dlcFile.open(QIODevice::WriteOnly));
        dlcFile.write(QByteArray(2048, 'D'));
        dlcFile.close();

        const QString dumpRoot = m_localRoot.filePath(QStringLiteral("Astro NoNpDrm"));
        QVERIFY(PackageInspector::looksLikeNoNpDrmDump(dumpRoot));

        // One dropped folder becomes two staged rows, one per detected part.
        QCOMPARE(device.addDrops({ dumpRoot }), 2);
        QCOMPARE(device.drops()->rowCount(), 2);

        QStringList destinations;
        QStringList titleIds;
        QStringList kindLabels;
        for (int row = 0; row < device.drops()->rowCount(); ++row) {
            const QModelIndex index = device.drops()->index(row, 0);
            destinations << device.drops()->data(index, DropStageModel::DestinationRole).toString();
            titleIds << device.drops()->data(index, DropStageModel::TitleIdRole).toString();
            kindLabels << device.drops()->data(index, DropStageModel::KindLabelRole).toString();
        }
        // Shown before anything is sent, and each part keeps its own serial.
        QVERIFY2(destinations.contains(QStringLiteral("/ux0:/app")), qPrintable(destinations.join(", ")));
        QVERIFY2(destinations.contains(QStringLiteral("/ux0:/addcont")), qPrintable(destinations.join(", ")));
        QCOMPARE(titleIds, QStringList({ QStringLiteral("PCSB00550"), QStringLiteral("PCSB00550") }));
        QVERIFY(kindLabels.contains(QStringLiteral("App Data")));
        QVERIFY(kindLabels.contains(QStringLiteral("Add-on Content")));

        QSignalSpy readySpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() == 2, 30000);

        QStringList landedPaths;
        for (const QList<QVariant> &call : std::as_const(readySpy)) {
            landedPaths << call.at(1).toString();
            // Same wording as any other folder-format install -- nothing
            // installs a NoNpDrm part except the console noticing it exists.
            QVERIFY2(call.at(2).toString().contains(QLatin1String("Refresh LiveArea")),
                     qPrintable(call.at(2).toString()));
        }
        QVERIFY(landedPaths.contains(QStringLiteral("/ux0:/app/PCSB00550")));
        QVERIFY(landedPaths.contains(QStringLiteral("/ux0:/addcont/PCSB00550")));

        const QString landedApp = devicePath(QStringLiteral("ux0:/app/PCSB00550"));
        QVERIFY(QFileInfo::exists(landedApp + QStringLiteral("/eboot.bin")));
        QVERIFY(QFileInfo::exists(landedApp + QStringLiteral("/sce_sys/param.sfo")));

        const QString landedAddcont = devicePath(QStringLiteral("ux0:/addcont/PCSB00550"));
        QFile landedDlc(landedAddcont + QStringLiteral("/subdir/dlc_data.bin"));
        QVERIFY(landedDlc.open(QIODevice::ReadOnly));
        QCOMPARE(landedDlc.readAll(), QByteArray(2048, 'D'));
    }

    void droppingAGameFolderSendsItToTheAppFolder()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString folder = makeGameFolder(QStringLiteral("Uncharted Folder"),
                                              QStringLiteral("PCSE00001"),
                                              QStringLiteral("Uncharted: Golden Abyss"));
        QVERIFY(!folder.isEmpty());

        QCOMPARE(device.addDrops({ folder }), 1);

        // The destination is shown before anything is sent.
        const QModelIndex row = device.drops()->index(0, 0);
        QCOMPARE(device.drops()->data(row, DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/app"));
        QCOMPARE(device.drops()->data(row, DropStageModel::TitleRole).toString(),
                 QStringLiteral("Uncharted: Golden Abyss"));

        QSignalSpy readySpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY2(readySpy.wait(30000), "the game folder never finished transferring");

        // Under its Title ID, not under the name the folder happened to have.
        QCOMPARE(readySpy.first().at(1).toString(), QStringLiteral("/ux0:/app/PCSE00001"));
        // And the wording is the one that actually applies to a folder game.
        const QString nextStep = readySpy.first().at(2).toString();
        QVERIFY2(nextStep.contains(QLatin1String("Refresh LiveArea")), qPrintable(nextStep));
        QVERIFY2(!nextStep.contains(QLatin1String("press X")), qPrintable(nextStep));

        const QString landed = devicePath(QStringLiteral("ux0:/app/PCSE00001"));
        QVERIFY(QFileInfo::exists(landed + QStringLiteral("/eboot.bin")));
        QVERIFY(QFileInfo::exists(landed + QStringLiteral("/sce_sys/param.sfo")));
        QVERIFY(QFileInfo::exists(landed + QStringLiteral("/sce_sys/icon0.png")));
        // The nested structure survives the trip.
        QVERIFY(QFileInfo::exists(landed + QStringLiteral("/sce_sys/livearea/contents/bg.png")));
        QVERIFY(QFileInfo::exists(landed + QStringLiteral("/module/libc.suprx")));

        QFile eboot(landed + QStringLiteral("/eboot.bin"));
        QVERIFY(eboot.open(QIODevice::ReadOnly));
        QCOMPARE(eboot.readAll(), QByteArray(4096, 'E'));

        // Nothing was copied to a staging area on the way.
        QVERIFY(!QFileInfo::exists(devicePath(QStringLiteral("ux0:/vpk/Uncharted Folder"))));
    }

    void standaloneAppAndAddcontFoldersRouteToDistinctDestinations()
    {
        // The bug found on hardware: dragging "app" and "addcont" separately
        // (not the NoNpDrm parent folder that wraps them) used to leave both
        // unrecognised, or -- if their inner serial folder was dragged
        // instead -- both defaulting to ux0:app/<serial> regardless of which
        // one they actually were, mixing add-on content into the game's own
        // folder. Each must land at its own destination.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString appWrapperGame =
            makeGameFolder(QStringLiteral("Standalone/app/PCSB00551"),
                           QStringLiteral("PCSB00551"), QStringLiteral("Some Game"));
        QVERIFY(!appWrapperGame.isEmpty());
        const QString appWrapper = m_localRoot.filePath(QStringLiteral("Standalone/app"));
        QVERIFY(PackageInspector::looksLikeStandaloneContentFolder(appWrapper));

        const QString addcontFolder =
            m_localRoot.filePath(QStringLiteral("Standalone/addcont/PCSB00551"));
        QVERIFY(QDir().mkpath(addcontFolder));
        QFile dlcFile(addcontFolder + QStringLiteral("/dlc.bin"));
        QVERIFY(dlcFile.open(QIODevice::WriteOnly));
        dlcFile.write(QByteArray(1024, 'D'));
        dlcFile.close();
        const QString addcontWrapper = m_localRoot.filePath(QStringLiteral("Standalone/addcont"));
        QVERIFY(PackageInspector::looksLikeStandaloneContentFolder(addcontWrapper));

        // Dragged separately, exactly as on hardware -- two different drops,
        // not one parent folder.
        QCOMPARE(device.addDrops({ appWrapper }), 1);
        QCOMPARE(device.addDrops({ addcontWrapper }), 1);
        QCOMPARE(device.drops()->rowCount(), 2);

        QStringList destinations;
        QStringList kindLabels;
        for (int row = 0; row < device.drops()->rowCount(); ++row) {
            const QModelIndex index = device.drops()->index(row, 0);
            destinations << device.drops()->data(index, DropStageModel::DestinationRole).toString();
            kindLabels << device.drops()->data(index, DropStageModel::KindLabelRole).toString();
        }
        QVERIFY2(destinations.contains(QStringLiteral("/ux0:/app")), qPrintable(destinations.join(", ")));
        QVERIFY2(destinations.contains(QStringLiteral("/ux0:/addcont")), qPrintable(destinations.join(", ")));
        QVERIFY2(destinations.at(0) != destinations.at(1),
                 qPrintable(QStringLiteral("both parts collapsed onto the same destination: %1")
                                .arg(destinations.join(", "))));
        QVERIFY(kindLabels.contains(QStringLiteral("App Data")));
        QVERIFY(kindLabels.contains(QStringLiteral("Add-on Content")));

        QSignalSpy readySpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QTRY_VERIFY_WITH_TIMEOUT(readySpy.count() == 2, 30000);

        QStringList landedPaths;
        for (const QList<QVariant> &call : std::as_const(readySpy))
            landedPaths << call.at(1).toString();
        QVERIFY(landedPaths.contains(QStringLiteral("/ux0:/app/PCSB00551")));
        QVERIFY(landedPaths.contains(QStringLiteral("/ux0:/addcont/PCSB00551")));

        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/app/PCSB00551/eboot.bin"))));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/addcont/PCSB00551/dlc.bin"))));
        // Not mixed into the game's own folder -- the exact corruption the
        // bug report described.
        QVERIFY(!QFileInfo::exists(devicePath(QStringLiteral("ux0:/app/PCSB00551/dlc.bin"))));
    }

    void standaloneLicenseFolderRoutesToLicenseDestination()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString licenseFolder =
            m_localRoot.filePath(QStringLiteral("StandaloneLicense/license/PCSB00552"));
        QVERIFY(QDir().mkpath(licenseFolder));
        QFile rif(licenseFolder + QStringLiteral("/app.rif"));
        QVERIFY(rif.open(QIODevice::WriteOnly));
        rif.write(QByteArray(64, 'L'));
        rif.close();

        const QString licenseWrapper = m_localRoot.filePath(QStringLiteral("StandaloneLicense/license"));
        QVERIFY(PackageInspector::looksLikeStandaloneContentFolder(licenseWrapper));
        QCOMPARE(device.addDrops({ licenseWrapper }), 1);

        const QModelIndex index = device.drops()->index(0, 0);
        QCOMPARE(device.drops()->data(index, DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/license"));
        QCOMPARE(device.drops()->data(index, DropStageModel::KindLabelRole).toString(),
                 QStringLiteral("License"));
    }

    void folderInstallMergesRatherThanReplacingExistingDestinationContent()
    {
        // Nothing deletes before uploading a folder-format install, so
        // dropping a game already present on the device must merge with
        // what's there -- overwriting files the new drop actually provides,
        // never touching anything else already in that destination folder.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        // Pre-seed the destination as if something unrelated already lived
        // there -- e.g. a leftover save-adjacent file from a previous state.
        const QString destDir = devicePath(QStringLiteral("ux0:/app/PCSE00001"));
        QVERIFY(QDir().mkpath(destDir));
        QFile leftover(destDir + QStringLiteral("/leftover.dat"));
        QVERIFY(leftover.open(QIODevice::WriteOnly));
        leftover.write(QByteArray(777, 'L'));
        leftover.close();
        QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/leftover.dat")));

        const QString folder = makeGameFolder(QStringLiteral("Merge Test Folder"),
                                              QStringLiteral("PCSE00001"),
                                              QStringLiteral("Merge Test Game"));
        QVERIFY(!folder.isEmpty());
        QCOMPARE(device.addDrops({ folder }), 1);

        QSignalSpy readySpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY2(readySpy.wait(30000), "the game folder never finished transferring");

        // What the drop actually provides landed...
        QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/eboot.bin")));
        QVERIFY(QFileInfo::exists(destDir + QStringLiteral("/sce_sys/param.sfo")));
        // ...and the file that was already there, and had nothing to do with
        // this drop, is untouched -- a folder install merges, it never wipes
        // the destination first.
        QVERIFY2(QFileInfo::exists(destDir + QStringLiteral("/leftover.dat")),
                 "an unrelated file already in the destination was deleted by the install");
        QFile leftoverAfter(destDir + QStringLiteral("/leftover.dat"));
        QVERIFY(leftoverAfter.open(QIODevice::ReadOnly));
        QCOMPARE(leftoverAfter.readAll(), QByteArray(777, 'L'));
    }

    void aPackagedGameStillSaysPressX()
    {
        // The two wordings must not drift into each other.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Packaged") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSE00055") }
        });
        const QString archive = m_localRoot.filePath(QStringLiteral("packaged.vpk"));
        QVERIFY(!writeArchive(archive, {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("eboot.bin"), QByteArray(2048, 'E'), false }
        }).isEmpty());

        QCOMPARE(device.addDrops({ archive }), 1);
        QCOMPARE(device.drops()->data(device.drops()->index(0, 0),
                                     DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/vpk"));

        QSignalSpy installSpy(&device, &VitaDevice::installReady);
        QSignalSpy folderSpy(&device, &VitaDevice::folderInstallReady);
        device.startStaged();
        QVERIFY(installSpy.wait(20000));
        QVERIFY2(folderSpy.isEmpty(), "a package must not report as a folder install");
    }

    void mediaIsRoutedWithoutBeingTreatedAsAPackage()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString clip = m_localRoot.filePath(QStringLiteral("Holiday.mp4"));
        QFile file(clip);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(64 * 1024, 'V'));
        file.close();

        QCOMPARE(device.addDrops({ clip }), 1);
        QCOMPARE(device.drops()->data(device.drops()->index(0, 0),
                                     DropStageModel::DestinationRole).toString(),
                 QStringLiteral("/ux0:/video"));

        QSignalSpy finishedSpy(device.transfers(), &TransferQueue::jobFinished);
        device.startStaged();
        QVERIFY(finishedSpy.wait(20000));
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/video/Holiday.mp4"))));
    }

    void downloadingASelectedFolderMirrorsIt()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));
        device.setDownloadDir(m_downloadRoot.path());

        device.navigateTo(QStringLiteral("/ux0:/user/00/savedata"));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        // Select the savedata folder the way the browser's selection would.
        device.browser()->selectAll();
        QVERIFY(device.browser()->selectionCount() > 0);

        QSignalSpy finishedSpy(device.transfers(), &TransferQueue::jobFinished);
        device.downloadSelection();
        QVERIFY2(finishedSpy.wait(25000), "the folder download never finished");

        const QString mirror = m_downloadRoot.filePath(QStringLiteral("PCSE00001"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(mirror + QStringLiteral("/data.bin")), 10000);
        QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/sub/n.bin")));
    }

    void downloadingAVpkLandsInItsOwnLocalFolder()
    {
        // A downloaded VPK should not end up loose next to savedata and
        // screenshots -- it gets its own "vpk" folder under the download
        // folder, the same way the device keeps its own content apart by kind.
        VitaDevice device;
        QVERIFY(connectDevice(&device));
        device.setDownloadDir(m_downloadRoot.path());

        device.navigateTo(QStringLiteral("/ux0:/vpk"));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);
        device.browser()->selectAll();
        QVERIFY(device.browser()->selectionCount() > 0);

        QSignalSpy finishedSpy(device.transfers(), &TransferQueue::jobFinished);
        device.downloadSelection();
        QVERIFY2(finishedSpy.wait(15000), "the vpk download never finished");

        const QString expected = m_downloadRoot.filePath(QStringLiteral("vpk/already.vpk"));
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(expected), 10000);
        QCOMPARE(QFileInfo(expected).size(), qint64(128));
        // Not loose directly in the download folder.
        QVERIFY(!QFileInfo::exists(m_downloadRoot.filePath(QStringLiteral("already.vpk"))));
    }

    // --- navigation -------------------------------------------------------
    //
    // These assert what the browser actually shows, not what the breadcrumb
    // says. Real hardware listed the same ten mount points for every folder
    // while the breadcrumb happily updated, because the server silently
    // answered a LIST it could not resolve with the working directory.

    void navigatingIntoAFolderChangesWhatIsListed()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        const QStringList atCard = listedNames(&device);
        QVERIFY2(atCard.contains(QStringLiteral("vpk")), qPrintable(atCard.join(", ")));
        QVERIFY2(atCard.contains(QStringLiteral("video")), qPrintable(atCard.join(", ")));

        device.navigateTo(QStringLiteral("/ux0:/video"));
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/ux0:/video"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(listedNames(&device) != atCard, 5000);

        const QStringList inVideo = listedNames(&device);
        // The contents really are the video folder's, not the card's.
        QVERIFY2(inVideo.contains(QStringLiteral("clip.mp4")), qPrintable(inVideo.join(", ")));
        QVERIFY2(!inVideo.contains(QStringLiteral("vpk")), qPrintable(inVideo.join(", ")));
        QVERIFY2(!inVideo.contains(QStringLiteral("os0:")), qPrintable(inVideo.join(", ")));
    }

    void everyFolderListsItsOwnContents()
    {
        // Three different folders, three different listings. The bug on
        // hardware produced one identical listing for all of them.
        VitaDevice device;
        QVERIFY(connectDevice(&device));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        QVector<QStringList> seen;
        const QStringList folders = {
            QStringLiteral("/ux0:"),
            QStringLiteral("/ux0:/video"),
            QStringLiteral("/ux0:/vpk"),
            QStringLiteral("/ux0:/user/00/savedata/PCSE00001")
        };

        for (const QString &folder : folders) {
            device.navigateTo(folder);
            QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == folder, 5000);
            // Let the listing that belongs to this path arrive.
            QTest::qWait(120);
            const QStringList names = listedNames(&device);
            QVERIFY2(!names.isEmpty(),
                     qPrintable(QStringLiteral("%1 listed nothing").arg(folder)));
            QVERIFY2(!names.contains(QStringLiteral("os0:")),
                     qPrintable(QStringLiteral("%1 fell back to the device list").arg(folder)));
            seen.append(names);
        }

        for (int i = 0; i < seen.size(); ++i) {
            for (int j = i + 1; j < seen.size(); ++j) {
                QVERIFY2(seen.at(i) != seen.at(j),
                         qPrintable(QStringLiteral("%1 and %2 listed identical contents: %3")
                                        .arg(folders.at(i), folders.at(j),
                                             seen.at(i).join(QStringLiteral(", ")))));
            }
        }
    }

    void navigatingToADeviceRootListsThatDevice()
    {
        // The entry point to the whole browser, and the case that broke: a
        // bare "ux0:" does not stat on hardware, so a LIST carrying that path
        // fell back to the mount-point list.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        device.navigateTo(QStringLiteral("/ux0:"));
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/ux0:"), 5000);
        QTest::qWait(120);

        const QStringList names = listedNames(&device);
        QVERIFY2(names.contains(QStringLiteral("vpk")), qPrintable(names.join(", ")));
        QVERIFY2(!names.contains(QStringLiteral("os0:")),
                 qPrintable(QStringLiteral("device root fell back to the mount list: %1")
                                .arg(names.join(QStringLiteral(", ")))));
    }

    void theRootReallyDoesListTheDevices()
    {
        // "/" is the one place the mount-point list is the right answer.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        device.navigateTo(QStringLiteral("/"));
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/"), 5000);
        QTest::qWait(120);

        const QStringList names = listedNames(&device);
        QVERIFY2(names.contains(QStringLiteral("ux0:")), qPrintable(names.join(", ")));
        QVERIFY2(names.contains(QStringLiteral("os0:")), qPrintable(names.join(", ")));
    }

    /// The path a sidebar click actually sends, found the same way the QML
    /// sidebar does: by id in Device.quickLocations(), never a literal.
    static QString quickLocationPath(VitaDevice *device, const QString &id)
    {
        const QVariantList locations = device->quickLocations();
        for (const QVariant &value : locations) {
            const QVariantMap map = value.toMap();
            if (map.value(QStringLiteral("id")).toString() == id)
                return map.value(QStringLiteral("path")).toString();
        }
        return {};
    }

    void sidebarRootListsEveryPartitionNotJustTheCurrentMount()
    {
        // Bug: "Root" pointed at "/ux0:" -- this mount's own contents -- so
        // it only ever showed the memory card, even though the device
        // reports ur0:, uma0:, imc0: and grw0: too (visible in the Link
        // screen's "once linked" chips). "Root" has to mean the true
        // filesystem root, the one place a LIST answers with every
        // partition the device actually has. See docs/browsing.md.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString rootPath = quickLocationPath(&device, QStringLiteral("root"));
        QVERIFY2(!rootPath.isEmpty(), "no 'root' quick location found");

        device.navigateTo(rootPath);
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == rootPath, 5000);
        QTest::qWait(120);

        const QStringList names = listedNames(&device);
        QVERIFY2(names.contains(QStringLiteral("ux0:")), qPrintable(names.join(", ")));
        QVERIFY2(names.contains(QStringLiteral("ur0:")), qPrintable(names.join(", ")));
        QVERIFY2(names.contains(QStringLiteral("uma0:")), qPrintable(names.join(", ")));
        // The wrong, pre-fix answer: nothing but what /ux0: itself holds.
        QVERIFY2(!names.contains(QStringLiteral("vpk")),
                 qPrintable(QStringLiteral("Root fell back to /ux0:'s own contents: %1")
                                .arg(names.join(QStringLiteral(", ")))));
    }

    void sidebarPackagesListsWhatIsActuallyThere()
    {
        // The sidebar's "Packages" entry and a plain folder navigation go
        // through the exact same Device.navigateTo() call; this pins that
        // the path the sidebar actually computes -- not a hand-typed
        // literal -- really does list real, populated content, so a
        // regression in how that entry is wired (as opposed to the
        // CWD/LIST protocol, already covered by everyFolderListsItsOwnContents)
        // would be caught here.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        const QString vpkPath = quickLocationPath(&device, QStringLiteral("vpk"));
        QCOMPARE(vpkPath, QStringLiteral("/ux0:/vpk"));

        device.navigateTo(vpkPath);
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == vpkPath, 5000);
        QTest::qWait(120);

        const QStringList names = listedNames(&device);
        QVERIFY2(names.contains(QStringLiteral("already.vpk")), qPrintable(names.join(", ")));
        QCOMPARE(device.statusTone(), QStringLiteral("ok"));
    }

    void goingBackUpListsTheParentAgain()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        device.navigateTo(QStringLiteral("/ux0:/user/00/savedata"));
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/ux0:/user/00/savedata"), 5000);
        QTest::qWait(120);
        const QStringList deep = listedNames(&device);
        QVERIFY(deep.contains(QStringLiteral("PCSE00001")));

        device.navigateUp();
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/ux0:/user/00"), 5000);
        QTest::qWait(120);
        const QStringList up = listedNames(&device);
        QVERIFY2(up.contains(QStringLiteral("savedata")), qPrintable(up.join(", ")));
        QVERIFY(up != deep);
    }

    void goingUpFromAMountRootReachesTheTrueRoot()
    {
        // "Up" from a mount root used to just stop, inconsistent with the
        // sidebar's "Root" entry, which already reaches the true filesystem
        // root (every partition the device has -- see docs/browsing.md).
        // The two must agree on what "the top" means.
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        device.navigateTo(QStringLiteral("/ux0:"));
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/ux0:"), 5000);
        QTest::qWait(120);

        device.navigateUp();
        QTRY_VERIFY_WITH_TIMEOUT(device.currentPath() == QStringLiteral("/"), 5000);
        QTest::qWait(120);
        const QStringList names = listedNames(&device);
        QVERIFY2(names.contains(QStringLiteral("ur0:")), qPrintable(names.join(", ")));

        // And the true root really is the top: one more "Up" does nothing.
        device.navigateUp();
        QTest::qWait(120);
        QCOMPARE(device.currentPath(), QStringLiteral("/"));
    }

    void deletingAProtectedFolderIsRefused()
    {
        VitaDevice device;
        QVERIFY(connectDevice(&device));

        device.navigateTo(QStringLiteral("/ux0:"));
        QTRY_VERIFY_WITH_TIMEOUT(device.browser()->rowCount() > 0, 5000);

        // "user" is firmware-owned; selecting and deleting must not touch it.
        int userRow = -1;
        for (int row = 0; row < device.browser()->rowCount(); ++row) {
            if (device.browser()->itemAt(row).value(QStringLiteral("name")).toString()
                == QLatin1String("user")) {
                userRow = row;
                break;
            }
        }
        QVERIFY(userRow >= 0);

        device.browser()->selectOnly(userRow);
        QSignalSpy notifySpy(&device, &VitaDevice::notify);
        device.deleteSelection();

        QVERIFY(notifySpy.count() > 0);
        QVERIFY(notifySpy.last().at(0).toString().contains(QLatin1String("system")));
        QVERIFY(QFileInfo::exists(devicePath(QStringLiteral("ux0:/user"))));
    }
};

QTEST_MAIN(DeviceTest)
#include "tst_device.moc"
