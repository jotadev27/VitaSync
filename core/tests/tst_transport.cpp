// End-to-end tests for the transport, driven against an in-process stand-in
// for VitaShell's FTP server. These cover the paths that only fail on a real
// socket: passive-mode negotiation, chunked uploads, and the verification step
// that decides whether a package is safe to install.

#include "MockVitaServer.h"
#include "TestArchives.h"

#include <vsp/ArchiveExtractor.h>
#include <vsp/FtpClient.h>
#include <vsp/PackageInspector.h>
#include <vsp/ThemeReader.h>
#include <vsp/VitaPaths.h>
#include <vsp/TransferQueue.h>
#include <vsp/ZipReader.h>

#include <QSignalSpy>
#include <QDirIterator>
#include <QTemporaryDir>
#include <QtTest>

using namespace vsp;
using namespace vsptest;

class TransportTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_deviceRoot;
    QTemporaryDir m_localRoot;
    MockVitaServer *m_server = nullptr;

    QString deviceFile(const QString &relative) const
    {
        return m_deviceRoot.filePath(relative);
    }

    static bool writeFile(const QString &path, const QByteArray &content)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(content) == content.size();
    }

    /// A PS Vita home-screen theme: a folder with theme.xml at its root,
    /// including a nested folder so the structure is really exercised.
    static bool writeThemeArchive(const QString &path)
    {
        return !writeArchive(path, {
            { QStringLiteral("Hu-Tao_Theme/theme.xml"),
              makeThemeXml(QStringLiteral("Hu-Tao"), QStringLiteral("CTRPlugin")), true },
            { QStringLiteral("Hu-Tao_Theme/br.png"), QByteArray(4096, 'B'), false },
            { QStringLiteral("Hu-Tao_Theme/preview_thumbnail.png"), QByteArray(512, 'T'), true },
            { QStringLiteral("Hu-Tao_Theme/img/deep.png"), QByteArray(256, 'D'), false }
        }).isEmpty();
    }

    /// A VitaShell skin: a folder with colors.txt and the documented PNGs.
    static bool writeShellThemeArchive(const QString &path)
    {
        return !writeArchive(path, {
            { QStringLiteral("Midnight/colors.txt"),
              QByteArray("BACKGROUND_COLOR = 0x00000000\n"), false },
            { QStringLiteral("Midnight/wallpaper.png"), QByteArray(2048, 'W'), true },
            { QStringLiteral("Midnight/folder_icon.png"), QByteArray(128, 'F'), false }
        }).isEmpty();
    }

    /// Connects a fresh client to the mock device, or fails the test.
    bool connectClient(FtpClient *client)
    {
        QSignalSpy spy(client, &FtpClient::connected);
        client->connectToDevice(QStringLiteral("127.0.0.1"), m_server->port());
        return spy.wait(5000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_deviceRoot.isValid());
        QVERIFY(m_localRoot.isValid());

        // Give the fake device the folder layout a real one has.
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/vpk"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/video"));
        QDir(m_deviceRoot.path()).mkpath(QStringLiteral("ux0:/user/00/savedata"));

        QFile save(deviceFile(QStringLiteral("ux0:/user/00/savedata/PCSE00001.bin")));
        QVERIFY(save.open(QIODevice::WriteOnly));
        save.write(QByteArray(4096, 'S'));
        save.close();

        m_server = new MockVitaServer(m_deviceRoot.path(), this);
        QVERIFY2(m_server->listen(), "mock device could not listen");
    }

    void connectsAndReportsGreeting()
    {
        FtpClient client;
        QSignalSpy connectedSpy(&client, &FtpClient::connected);

        const int id = client.connectToDevice(QStringLiteral("127.0.0.1"), m_server->port());
        QVERIFY(id > 0);
        QVERIFY(connectedSpy.wait(5000));
        QVERIFY(client.isConnected());
        QVERIFY(connectedSpy.first().first().toString().contains(QLatin1String("VitaShell")));
        QVERIFY(client.systemType().contains(QLatin1String("UNIX")));
    }

    void rejectsMalformedAddressWithoutConnecting()
    {
        FtpClient client;
        QSignalSpy errorSpy(&client, &FtpClient::errorOccurred);
        // A port outside the valid range must never reach a socket.
        QCOMPARE(client.connectToDevice(QStringLiteral("127.0.0.1"), 0), 0);
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!client.isConnected());
    }

    void listsRemoteDirectory()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        QSignalSpy listingSpy(&client, &FtpClient::listingReady);
        const int id = client.list(QStringLiteral("/ux0:"));
        QVERIFY(id > 0);
        QVERIFY(listingSpy.wait(5000));

        const auto entries = listingSpy.first().at(2).value<QVector<RemoteEntry>>();
        QStringList names;
        for (const RemoteEntry &entry : entries)
            names.append(entry.name);

        QVERIFY(names.contains(QStringLiteral("vpk")));
        QVERIFY(names.contains(QStringLiteral("video")));
        for (const RemoteEntry &entry : entries)
            QVERIFY(entry.isDirectory);
    }

    void uploadsFileAndReportsProgress()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        // Big enough that the upload really is chunked across bytesWritten.
        const QByteArray payload(700 * 1024, 'V');
        const QString localPath = m_localRoot.filePath(QStringLiteral("game.vpk"));
        QFile local(localPath);
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(payload);
        local.close();

        QSignalSpy progressSpy(&client, &FtpClient::transferProgress);
        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);

        const int id = client.upload(localPath, QStringLiteral("/ux0:/vpk/game.vpk"));
        QVERIFY(id > 0);
        QVERIFY(finishedSpy.wait(10000));
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));

        // The file really landed, byte for byte.
        QFile landed(deviceFile(QStringLiteral("ux0:/vpk/game.vpk")));
        QVERIFY(landed.open(QIODevice::ReadOnly));
        QCOMPARE(landed.readAll(), payload);

        // And progress was reported incrementally rather than once at the end.
        QVERIFY(progressSpy.count() > 1);
        QCOMPARE(progressSpy.last().at(1).toLongLong(), qint64(payload.size()));
    }

    void downloadsFileIntoChosenFolder()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        const QString target = m_localRoot.filePath(QStringLiteral("PCSE00001.bin"));
        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);

        client.download(QStringLiteral("/ux0:/user/00/savedata/PCSE00001.bin"), target);
        QVERIFY(finishedSpy.wait(10000));
        QVERIFY(finishedSpy.last().at(1).toBool());

        QFile file(target);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(4096, 'S'));
    }

    void reportsRemoteSize()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        QSignalSpy sizeSpy(&client, &FtpClient::sizeReady);
        client.requestSize(QStringLiteral("/ux0:/user/00/savedata/PCSE00001.bin"));
        QVERIFY(sizeSpy.wait(5000));
        QCOMPARE(sizeSpy.first().at(2).toLongLong(), 4096LL);
    }

    void createsRenamesAndDeletes()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));
        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);

        client.makeDirectory(QStringLiteral("/ux0:/vpk/nested"));
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(QFileInfo::exists(deviceFile(QStringLiteral("ux0:/vpk/nested"))));

        finishedSpy.clear();
        client.rename(QStringLiteral("/ux0:/vpk/nested"), QStringLiteral("/ux0:/vpk/renamed"));
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(QFileInfo::exists(deviceFile(QStringLiteral("ux0:/vpk/renamed"))));

        finishedSpy.clear();
        client.removeDirectory(QStringLiteral("/ux0:/vpk/renamed"));
        QVERIFY(finishedSpy.wait(5000));
        QVERIFY(!QFileInfo::exists(deviceFile(QStringLiteral("ux0:/vpk/renamed"))));
    }

    void queuedRequestsRunInOrder()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);
        const int first = client.makeDirectory(QStringLiteral("/ux0:/vpk/a"));
        const int second = client.makeDirectory(QStringLiteral("/ux0:/vpk/b"));
        const int third = client.list(QStringLiteral("/ux0:/vpk"));

        QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 3, 10000);
        QCOMPARE(finishedSpy.at(0).at(0).toInt(), first);
        QCOMPARE(finishedSpy.at(1).at(0).toInt(), second);
        QCOMPARE(finishedSpy.at(2).at(0).toInt(), third);
    }

    // --- the queue, including verification --------------------------------

    void queueVerifiesUploadAndWaitsForTheDevice()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        const QString localPath = m_localRoot.filePath(QStringLiteral("verified.vpk"));
        QFile local(localPath);
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(QByteArray(64 * 1024, 'Q'));
        local.close();

        PackageInfo info;
        info.valid = true;
        info.kind = PackageKind::Game;
        info.localPath = localPath;
        info.fileName = QStringLiteral("verified.vpk");
        info.fileSize = 64 * 1024;
        info.titleId = QStringLiteral("PCSE00001");
        info.title = QStringLiteral("Test Title");

        QSignalSpy readySpy(&queue, &TransferQueue::installReady);
        queue.enqueueUpload(info, QStringLiteral("/ux0:/vpk"), true);
        queue.start();

        QVERIFY2(readySpy.wait(15000), "upload never reached the install-ready state");
        QCOMPARE(readySpy.first().at(3).toString(), QStringLiteral("/ux0:/vpk/verified.vpk"));

        // The job stays visible as "waiting for the device" rather than being
        // reported as installed, because the app cannot install it.
        const QModelIndex index = queue.index(0, 0);
        QCOMPARE(queue.data(index, TransferQueue::StateRole).toInt(),
                 static_cast<int>(TransferQueue::JobState::AwaitingDevice));
        QVERIFY(queue.data(index, TransferQueue::VerifiedRole).toBool());
    }

    void queueFailsTheJobWhenTheDeviceReportsAWrongSize()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        m_server->setCorruptSizeReports(true);
        const auto restore = qScopeGuard([this] { m_server->setCorruptSizeReports(false); });

        const QString localPath = m_localRoot.filePath(QStringLiteral("corrupt.vpk"));
        QFile local(localPath);
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(QByteArray(32 * 1024, 'C'));
        local.close();

        PackageInfo info;
        info.valid = true;
        info.kind = PackageKind::Game;
        info.localPath = localPath;
        info.fileName = QStringLiteral("corrupt.vpk");
        info.fileSize = 32 * 1024;
        info.titleId = QStringLiteral("PCSE00002");
        info.title = QStringLiteral("Corrupt Title");

        QSignalSpy finishedSpy(&queue, &TransferQueue::jobFinished);
        QSignalSpy readySpy(&queue, &TransferQueue::installReady);
        queue.enqueueUpload(info, QStringLiteral("/ux0:/vpk"), true);
        queue.start();

        QVERIFY2(finishedSpy.wait(15000), "the job never finished");
        QVERIFY2(!finishedSpy.last().at(1).toBool(), "a size mismatch must fail the job");
        QVERIFY2(readySpy.isEmpty(), "a mismatched upload must never be offered for install");
        QVERIFY(finishedSpy.last().at(2).toString().contains(QLatin1String("mismatch")));
    }

    void queueDownloadsIntoTheChosenFolderOnly()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        QTemporaryDir destination;
        QVERIFY(destination.isValid());


        // Whatever a hostile remote path normalises to, the file it produces has
        // to land inside the folder the user picked. Checked on a detached
        // queue, so the assertion is about the path and nothing actually runs.
        {
            TransferQueue offline;
            const int id = offline.enqueueDownload(
                QStringLiteral("/ux0:/user/00/savedata/../../../../etc/passwd"), 0,
                destination.path(), QString());
            QVERIFY(id > 0);
            const QString landing = offline.data(offline.index(0, 0),
                                                 TransferQueue::LocalPathRole).toString();
            QVERIFY2(landing.startsWith(QDir::cleanPath(destination.path())),
                     qPrintable(QStringLiteral("escaped to %1").arg(landing)));
        }

        QSignalSpy finishedSpy(&queue, &TransferQueue::jobFinished);
        const int id = queue.enqueueDownload(
            QStringLiteral("/ux0:/user/00/savedata/PCSE00001.bin"), 4096,
            destination.path(), QStringLiteral("PCSE00001.bin"));
        QVERIFY(id > 0);
        queue.start();

        QVERIFY(finishedSpy.wait(15000));
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));
        QVERIFY(QFileInfo::exists(destination.filePath(QStringLiteral("PCSE00001.bin"))));
    }
    // --- navigation protocol ---------------------------------------------

    void listingNavigatesWithCwdAndListsTheWorkingDirectory()
    {
        // Pins the protocol, not just the outcome. VitaShell's server accepts
        // a path argument on LIST but uses it only if it stats, and silently
        // answers with the working directory otherwise -- so a client that
        // passes the path and never sends CWD gets the mount-point list for
        // every folder. Anyone tempted to "simplify" this back into a single
        // LIST command should see this test fail.
        FtpClient client;
        QVERIFY(connectClient(&client));

        m_server->clearCommandLog();
        QSignalSpy listingSpy(&client, &FtpClient::listingReady);
        client.list(QStringLiteral("/ux0:/vpk"));
        QVERIFY(listingSpy.wait(5000));

        const QStringList commands = m_server->commandLog();
        const int cwdIndex = commands.indexOf(QStringLiteral("CWD /ux0:/vpk"));
        QVERIFY2(cwdIndex >= 0, qPrintable(commands.join(QStringLiteral(" | "))));

        // LIST must carry no argument: the working directory is the contract.
        const int listIndex = commands.indexOf(QStringLiteral("LIST"));
        QVERIFY2(listIndex > cwdIndex, qPrintable(commands.join(QStringLiteral(" | "))));

        for (const QString &command : commands) {
            QVERIFY2(!command.startsWith(QLatin1String("LIST ")),
                     qPrintable(QStringLiteral("LIST was given a path: %1").arg(command)));
        }
    }

    void listingADeviceRootReturnsThatDeviceNotTheMountList()
    {
        // The exact hardware symptom: "ux0:" does not stat, so a LIST carrying
        // it fell back to the list of mount points.
        FtpClient client;
        QVERIFY(connectClient(&client));

        QSignalSpy listingSpy(&client, &FtpClient::listingReady);
        client.list(QStringLiteral("/ux0:"));
        QVERIFY(listingSpy.wait(5000));

        QStringList names;
        for (const RemoteEntry &entry : listingSpy.first().at(2).value<QVector<RemoteEntry>>())
            names << entry.name;

        QVERIFY2(names.contains(QStringLiteral("vpk")), qPrintable(names.join(", ")));
        QVERIFY2(!names.contains(QStringLiteral("os0:")),
                 qPrintable(QStringLiteral("fell back to the mount list: %1")
                                .arg(names.join(QStringLiteral(", ")))));
    }

    void listingARefusedFolderFailsInsteadOfShowingSomethingElse()
    {
        // A folder that is not there must produce an error, never a listing of
        // whatever the server happened to be sitting in.
        FtpClient client;
        QVERIFY(connectClient(&client));

        QSignalSpy listingSpy(&client, &FtpClient::listingReady);
        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);
        client.list(QStringLiteral("/ux0:/definitely-not-here"));

        QVERIFY(finishedSpy.wait(5000));
        QVERIFY2(!finishedSpy.last().at(1).toBool(), "a missing folder must fail");
        QVERIFY2(listingSpy.isEmpty(), "a failed listing must not emit entries");
    }

    void cancellingMidTransferLeavesTheChannelInStep()
    {
        // Alignment check: after an abandoned upload, a later query must come
        // back with its own answer. When the channel is a reply out of step
        // the value returned belongs to the command before it.
        FtpClient client;
        QVERIFY(connectClient(&client));

        const QByteArray payload(6 * 1024 * 1024, 'C');
        const QString localPath = m_localRoot.filePath(QStringLiteral("abandoned.bin"));
        QFile local(localPath);
        QVERIFY(local.open(QIODevice::WriteOnly));
        local.write(payload);
        local.close();

        QSignalSpy progressSpy(&client, &FtpClient::transferProgress);
        client.upload(localPath, QStringLiteral("/ux0:/vpk/abandoned.bin"));
        QVERIFY2(progressSpy.wait(10000), "the upload never started");

        client.abortAll();

        // A known file, so the answer is checkable rather than merely present.
        QSignalSpy sizeSpy(&client, &FtpClient::sizeReady);
        QSignalSpy finishedSpy(&client, &FtpClient::commandFinished);
        client.requestSize(QStringLiteral("/ux0:/user/00/savedata/PCSE00001.bin"));

        QVERIFY2(finishedSpy.wait(10000), "the request after a cancel never finished");
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));
        QCOMPARE(sizeSpy.count(), 1);
        QCOMPARE(sizeSpy.first().at(2).toLongLong(), 4096LL);
    }

    void cancellingWithNothingInFlightIsHarmless()
    {
        FtpClient client;
        QVERIFY(connectClient(&client));

        client.abortAll();          // nothing running

        QSignalSpy listingSpy(&client, &FtpClient::listingReady);
        client.list(QStringLiteral("/ux0:/vpk"));
        QVERIFY(listingSpy.wait(5000));
    }

    // --- theme install ----------------------------------------------------

    void themeInstallSendsTheWholeFolder()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        const QString archivePath = m_localRoot.filePath(QStringLiteral("HuTao.zip"));
        QVERIFY(writeThemeArchive(archivePath));

        PackageInfo info = PackageInspector::inspect(archivePath);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::SystemTheme);

        QSignalSpy readySpy(&queue, &TransferQueue::folderInstallReady);
        const int id = queue.enqueueFolderInstall(info, QStringLiteral("/ux0:/customtheme"));
        QVERIFY(id > 0);
        queue.start();

        QVERIFY2(readySpy.wait(20000), "the theme never finished transferring");
        QCOMPARE(readySpy.first().at(2).toString(),
                 QStringLiteral("/ux0:/customtheme/Hu-Tao_Theme"));
        // The next step names the tool that finishes the job on the device.
        QVERIFY(readySpy.first().at(3).toString().contains(QLatin1String("Custom Themes Manager")));

        // Every file arrived, including the one in a subfolder, with its bytes
        // intact and its structure preserved.
        const QString root = m_deviceRoot.path() + QStringLiteral("/ux0:/customtheme/Hu-Tao_Theme");
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/theme.xml")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/br.png")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/preview_thumbnail.png")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/img/deep.png")));

        QFile landed(root + QStringLiteral("/br.png"));
        QVERIFY(landed.open(QIODevice::ReadOnly));
        QCOMPARE(landed.readAll(), QByteArray(4096, 'B'));

        // The job reports as waiting on the device, not as installed.
        const QModelIndex index = queue.index(0, 0);
        QCOMPARE(queue.data(index, TransferQueue::StateRole).toInt(),
                 static_cast<int>(TransferQueue::JobState::AwaitingDevice));
        QCOMPARE(queue.data(index, TransferQueue::FileProgressRole).toString(),
                 QStringLiteral("4 / 4 files"));
    }

    void shellThemeGoesToVitaShellsOwnFolder()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        const QString archivePath = m_localRoot.filePath(QStringLiteral("Midnight.zip"));
        QVERIFY(writeShellThemeArchive(archivePath));

        const PackageInfo info = PackageInspector::inspect(archivePath);
        QCOMPARE(info.kind, PackageKind::ShellTheme);

        QSignalSpy readySpy(&queue, &TransferQueue::folderInstallReady);
        queue.enqueueFolderInstall(info, vitapaths::shellThemeDir());
        queue.start();

        QVERIFY2(readySpy.wait(20000), "the shell theme never finished");
        QCOMPARE(readySpy.first().at(2).toString(),
                 QStringLiteral("/ux0:/VitaShell/theme/Midnight"));
        QVERIFY(readySpy.first().at(3).toString().contains(QLatin1String("VitaShell")));

        const QString root = m_deviceRoot.path()
                             + QStringLiteral("/ux0:/VitaShell/theme/Midnight");
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/colors.txt")));
        QVERIFY(QFileInfo::exists(root + QStringLiteral("/wallpaper.png")));
    }

    // --- folder download --------------------------------------------------

    void folderDownloadMirrorsTheWholeTree()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        // A savedata folder with a subfolder and an empty folder in it.
        const QString base = m_deviceRoot.path() + QStringLiteral("/ux0:/user/00/savedata/PCSE00042");
        QVERIFY(QDir().mkpath(base + QStringLiteral("/sub")));
        QVERIFY(QDir().mkpath(base + QStringLiteral("/empty")));
        QVERIFY(writeFile(base + QStringLiteral("/data.bin"), QByteArray(3000, 'D')));
        QVERIFY(writeFile(base + QStringLiteral("/icon.png"), QByteArray(700, 'I')));
        QVERIFY(writeFile(base + QStringLiteral("/sub/nested.bin"), QByteArray(1500, 'N')));

        QTemporaryDir destination;
        QVERIFY(destination.isValid());

        QSignalSpy finishedSpy(&queue, &TransferQueue::jobFinished);
        const int id = queue.enqueueFolderDownload(
            QStringLiteral("/ux0:/user/00/savedata/PCSE00042"),
            destination.path(), QStringLiteral("PCSE00042"));
        QVERIFY(id > 0);
        queue.start();

        QVERIFY2(finishedSpy.wait(20000), "the folder download never finished");
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));

        const QString mirror = destination.filePath(QStringLiteral("PCSE00042"));
        QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/data.bin")));
        QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/icon.png")));
        QVERIFY(QFileInfo::exists(mirror + QStringLiteral("/sub/nested.bin")));
        // An empty remote folder still has to appear in the mirror.
        QVERIFY(QFileInfo(mirror + QStringLiteral("/empty")).isDir());

        QFile nested(mirror + QStringLiteral("/sub/nested.bin"));
        QVERIFY(nested.open(QIODevice::ReadOnly));
        QCOMPARE(nested.readAll(), QByteArray(1500, 'N'));

        // The total is the sum of the files, and the job reports every one.
        const QModelIndex index = queue.index(queue.rowCount() - 1, 0);
        QCOMPARE(queue.data(index, TransferQueue::FileProgressRole).toString(),
                 QStringLiteral("3 / 3 files"));
    }

    void emptyFolderDownloadStillProducesTheFolder()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        QVERIFY(QDir().mkpath(m_deviceRoot.path() + QStringLiteral("/ux0:/video/holiday")));

        QTemporaryDir destination;
        QSignalSpy finishedSpy(&queue, &TransferQueue::jobFinished);
        queue.enqueueFolderDownload(QStringLiteral("/ux0:/video/holiday"),
                                    destination.path(), QStringLiteral("holiday"));
        queue.start();

        QVERIFY(finishedSpy.wait(15000));
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(finishedSpy.last().at(2).toString()));
        QVERIFY(QFileInfo(destination.filePath(QStringLiteral("holiday"))).isDir());
    }

    void folderDownloadKeepsHostileNamesInsideTheMirror()
    {
        // The scanner already drops listing entries containing separators, so
        // this checks the second line of defence: whatever reaches the plan is
        // rebuilt from sanitised components under the chosen folder.
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        const QString base = m_deviceRoot.path() + QStringLiteral("/ux0:/video/odd");
        QVERIFY(QDir().mkpath(base));
        QVERIFY(writeFile(base + QStringLiteral("/..hidden.bin"), QByteArray(64, 'H')));
        QVERIFY(writeFile(base + QStringLiteral("/normal.bin"), QByteArray(64, 'N')));

        QTemporaryDir destination;
        QSignalSpy finishedSpy(&queue, &TransferQueue::jobFinished);
        queue.enqueueFolderDownload(QStringLiteral("/ux0:/video/odd"),
                                    destination.path(), QStringLiteral("odd"));
        queue.start();

        QVERIFY(finishedSpy.wait(15000));

        const QString root = QDir::cleanPath(destination.path());
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        int seen = 0;
        while (it.hasNext()) {
            const QString file = QDir::cleanPath(it.next());
            QVERIFY2(file.startsWith(root + QLatin1Char('/')),
                     qPrintable(QStringLiteral("escaped to %1").arg(file)));
            ++seen;
        }
        QVERIFY(seen > 0);
    }

    void cancellingATreeJobStopsItAndCleansUp()
    {
        FtpClient client;
        TransferQueue queue;
        queue.attach(&client, nullptr);
        QVERIFY(connectClient(&client));

        const QString archivePath = m_localRoot.filePath(QStringLiteral("Cancel.zip"));
        QVERIFY(writeThemeArchive(archivePath));
        const PackageInfo info = PackageInspector::inspect(archivePath);

        const int id = queue.enqueueFolderInstall(info, QStringLiteral("/ux0:/customtheme"));
        QVERIFY(id > 0);
        queue.start();

        queue.cancelJob(id);
        const QModelIndex index = queue.index(queue.rowCount() - 1, 0);
        QCOMPARE(queue.data(index, TransferQueue::StateRole).toInt(),
                 static_cast<int>(TransferQueue::JobState::Cancelled));

        // The queue must be idle afterwards rather than stuck on a dead job.
        QTRY_VERIFY_WITH_TIMEOUT(!queue.property("busy").toBool(), 5000);
    }
};

QTEST_MAIN(TransportTest)
#include "tst_transport.moc"
