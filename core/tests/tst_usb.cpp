// Tests for UsbTransport -- the USB implementation of RemoteTransport.
//
// There is no protocol to fake here the way MockVitaServer fakes VitaShell's
// FTP server: VitaShell's USB mode hands the raw partition to the host as a
// standard USB Mass Storage device, so from the app's side it is just a
// mounted folder (see docs/usb.md). These tests stand in a plain directory
// carrying the same signature (id.dat + a VitaShell install folder) and drive
// UsbTransport against it exactly as TransferQueue would.

#include <vsp/PathUtils.h>
#include <vsp/UsbTransport.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace vsp;

class UsbTransportTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_workRoot;
    QString m_volumeRoot;
    QString m_localRoot;

    QString volumeFile(const QString &relative) const
    {
        return QDir(m_volumeRoot).filePath(relative);
    }

    static bool writeFile(const QString &path, const QByteArray &content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        return file.write(content) == content.size();
    }

    /// Builds a fresh directory carrying the Vita USB signature, with the
    /// same top-level layout a real card has.
    QString makeSignedVolume(const QString &name) const
    {
        const QString root = QDir(m_workRoot.path()).filePath(name);
        QDir().mkpath(root);
        QDir(root).mkpath(QStringLiteral("app/VITASHELL"));
        QDir(root).mkpath(QStringLiteral("vpk"));
        QDir(root).mkpath(QStringLiteral("video"));
        writeFile(QDir(root).filePath(QStringLiteral("id.dat")), QByteArray(32, '\0'));
        return root;
    }

    bool attach(UsbTransport *usb, const QString &root)
    {
        QSignalSpy spy(usb, &RemoteTransport::connected);
        if (!usb->attachToMount(root))
            return false;
        return spy.count() == 1;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_workRoot.isValid());
        m_volumeRoot = makeSignedVolume(QStringLiteral("card"));
        m_localRoot = QDir(m_workRoot.path()).filePath(QStringLiteral("pc_side"));
        QVERIFY(QDir().mkpath(m_localRoot));

        writeFile(volumeFile(QStringLiteral("vpk/already.vpk")), QByteArray(128, 'P'));
        writeFile(volumeFile(QStringLiteral("video/clip.mp4")), QByteArray(4096, 'V'));
    }

    // --- detection -----------------------------------------------------

    void signatureRequiresBothIdDatAndVitaShell()
    {
        QVERIFY(UsbTransport::looksLikeVitaVolume(m_volumeRoot));

        const QString bare = QDir(m_workRoot.path()).filePath(QStringLiteral("bare_drive"));
        QDir().mkpath(bare);
        writeFile(QDir(bare).filePath(QStringLiteral("some_file.txt")), "not a vita");
        QVERIFY(!UsbTransport::looksLikeVitaVolume(bare));

        // id.dat alone (no VitaShell folder) is not enough either -- some
        // other device could coincidentally carry a file of that name.
        const QString idOnly = QDir(m_workRoot.path()).filePath(QStringLiteral("id_only"));
        QDir().mkpath(idOnly);
        writeFile(QDir(idOnly).filePath(QStringLiteral("id.dat")), QByteArray(4, '\0'));
        QVERIFY(!UsbTransport::looksLikeVitaVolume(idOnly));
    }

    void detectCandidatesFindsOnlySignedVolumes()
    {
        const QString unsigned1 = QDir(m_workRoot.path()).filePath(QStringLiteral("unsigned_usb"));
        QDir().mkpath(unsigned1);

        const QVector<UsbTransport::Candidate> found =
            UsbTransport::detectCandidates({ m_volumeRoot, unsigned1, QStringLiteral("/no/such/path") });

        QCOMPARE(found.size(), 1);
        QCOMPARE(QDir(found.first().rootPath).absolutePath(), QDir(m_volumeRoot).absolutePath());
    }

    // --- attach/detach ---------------------------------------------------

    void attachToSignedVolumeReportsVerified()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));
        QVERIFY(usb.isConnected());
        QVERIFY(usb.isVerified());
        usb.detachFromMount();
        QVERIFY(!usb.isConnected());
    }

    void attachToUnrecognisedFolderStillConnectsButUnverified()
    {
        const QString plain = QDir(m_workRoot.path()).filePath(QStringLiteral("plain_folder"));
        QDir().mkpath(plain);

        UsbTransport usb;
        QVERIFY(attach(&usb, plain));
        QVERIFY(usb.isConnected());
        QVERIFY(!usb.isVerified());
    }

    void attachRejectsAPathThatIsNotADirectory()
    {
        const QString file = QDir(m_workRoot.path()).filePath(QStringLiteral("just_a_file.txt"));
        writeFile(file, "x");

        UsbTransport usb;
        QSignalSpy errorSpy(&usb, &RemoteTransport::errorOccurred);
        QVERIFY(!usb.attachToMount(file));
        QVERIFY(!usb.isConnected());
        QCOMPARE(errorSpy.count(), 1);
    }

    // --- listing ---------------------------------------------------------

    void listsTheMappedDirectory()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        QSignalSpy listingSpy(&usb, &RemoteTransport::listingReady);
        const int id = usb.list(QStringLiteral("/ux0:"));
        QVERIFY(id > 0);
        QVERIFY(listingSpy.wait(5000));

        const auto entries = listingSpy.first().at(2).value<QVector<RemoteEntry>>();
        QVERIFY(entries.size() >= 3); // app, vpk, video

        bool sawVpk = false;
        for (const RemoteEntry &entry : entries) {
            if (entry.name == QLatin1String("vpk")) {
                sawVpk = true;
                QVERIFY(entry.isDirectory);
                QCOMPARE(entry.path, QStringLiteral("/ux0:/vpk"));
            }
        }
        QVERIFY(sawVpk);
    }

    void rejectsAMountOtherThanUx0()
    {
        // Real-hardware finding: Root (and anything else outside ux0:, which
        // is all USB can ever reach -- see docs/usb.md) felt slow/hung
        // instead of failing cleanly, because every failure -- including
        // this one, which never touches the mounted volume at all --
        // triggered a synchronous QStorageInfo stat that can be genuinely
        // slow against a real device. This must be rejected immediately and
        // without so much as looking at whether the volume is still there.
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        QSignalSpy finishedSpy(&usb, &RemoteTransport::commandFinished);
        QSignalSpy connectionLostSpy(&usb, &RemoteTransport::connectionLost);
        QSignalSpy disconnectedSpy(&usb, &RemoteTransport::disconnected);

        QElapsedTimer timer;
        timer.start();
        const int id = usb.list(QStringLiteral("/ur0:/somewhere"));
        QVERIFY(id > 0);
        QVERIFY(!finishedSpy.isEmpty() || finishedSpy.wait(5000));
        QCOMPARE(finishedSpy.first().at(1).toBool(), false);

        // Answered on the next event-loop turn -- not literally inline
        // (every caller assigns the returned request id before a reply can
        // arrive; see rejectRequest()'s own comment) -- but with zero
        // worker-thread round trips and no I/O of any kind.
        QVERIFY2(timer.elapsed() < 200,
                 qPrintable(QStringLiteral("took %1ms to reject an out-of-scope path")
                                .arg(timer.elapsed())));
        // And it never so much as asked whether the volume was still there:
        // a request that was invalid before it started can't mean the
        // device went away.
        QCOMPARE(connectionLostSpy.count(), 0);
        QCOMPARE(disconnectedSpy.count(), 0);
        QVERIFY(usb.isConnected());
    }

    // --- transfer ----------------------------------------------------------

    void uploadsThenDownloadsAFile()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        const QString localSource = QDir(m_localRoot).filePath(QStringLiteral("roundtrip.bin"));
        const QByteArray payload(3 * 1024 * 1024, 'X'); // several progress chunks
        QVERIFY(writeFile(localSource, payload));

        QSignalSpy uploadFinished(&usb, &RemoteTransport::commandFinished);
        QSignalSpy progressSpy(&usb, &RemoteTransport::transferProgress);
        const int uploadId = usb.upload(localSource, QStringLiteral("/ux0:/vpk/roundtrip.bin"));
        QVERIFY(uploadId > 0);
        QVERIFY(uploadFinished.wait(10000));
        QCOMPARE(uploadFinished.first().at(0).toInt(), uploadId);
        QCOMPARE(uploadFinished.first().at(1).toBool(), true);
        QVERIFY(!progressSpy.isEmpty());
        QCOMPARE(progressSpy.last().at(1).toLongLong(), qint64(payload.size()));

        QFile landed(volumeFile(QStringLiteral("vpk/roundtrip.bin")));
        QVERIFY(landed.exists());
        QCOMPARE(landed.size(), qint64(payload.size()));

        const QString localTarget = QDir(m_localRoot).filePath(QStringLiteral("roundtrip_back.bin"));
        QSignalSpy downloadFinished(&usb, &RemoteTransport::commandFinished);
        const int downloadId = usb.download(QStringLiteral("/ux0:/vpk/roundtrip.bin"), localTarget);
        QVERIFY(downloadId > 0);
        QVERIFY(downloadFinished.wait(10000));
        QCOMPARE(downloadFinished.first().at(1).toBool(), true);

        QFile back(localTarget);
        QVERIFY(back.open(QIODevice::ReadOnly));
        QCOMPARE(back.readAll(), payload);
    }

    void cancelStopsAnUploadWithoutDroppingTheConnection()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        const QString localSource = QDir(m_localRoot).filePath(QStringLiteral("big.bin"));
        QVERIFY(writeFile(localSource, QByteArray(24 * 1024 * 1024, 'Y')));

        QSignalSpy finishedSpy(&usb, &RemoteTransport::commandFinished);
        const int id = usb.upload(localSource, QStringLiteral("/ux0:/vpk/big.bin"));
        QVERIFY(id > 0);
        // abortAll() answers the cancelled request immediately, the same way
        // FtpClient::abortAll() does not wait for the socket teardown either.
        usb.abortAll();

        QVERIFY(!finishedSpy.isEmpty() || finishedSpy.wait(5000));
        QCOMPARE(finishedSpy.first().at(0).toInt(), id);
        QCOMPARE(finishedSpy.first().at(1).toBool(), false);
        QVERIFY(usb.isConnected());
    }

    // --- file management ---------------------------------------------------

    void mkdirFailsOnAnExistingFolderLikeRealMkd()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        QSignalSpy existingSpy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.makeDirectory(QStringLiteral("/ux0:/vpk")) > 0);
        QVERIFY(existingSpy.wait(5000));
        QCOMPARE(existingSpy.first().at(1).toBool(), false);

        QSignalSpy freshSpy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.makeDirectory(QStringLiteral("/ux0:/newfolder")) > 0);
        QVERIFY(freshSpy.wait(5000));
        QCOMPARE(freshSpy.first().at(1).toBool(), true);
        QVERIFY(QDir(volumeFile(QStringLiteral("newfolder"))).exists());
    }

    void removesFilesAndWholeDirectories()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        writeFile(volumeFile(QStringLiteral("temp/keep_me/nested.bin")), "x");
        QVERIFY(QFileInfo::exists(volumeFile(QStringLiteral("temp/keep_me/nested.bin"))));

        QSignalSpy dirSpy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.removeDirectory(QStringLiteral("/ux0:/temp")) > 0);
        QVERIFY(dirSpy.wait(5000));
        QCOMPARE(dirSpy.first().at(1).toBool(), true);
        QVERIFY(!QDir(volumeFile(QStringLiteral("temp"))).exists());

        QSignalSpy fileSpy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.removeFile(QStringLiteral("/ux0:/video/clip.mp4")) > 0);
        QVERIFY(fileSpy.wait(5000));
        QCOMPARE(fileSpy.first().at(1).toBool(), true);
        QVERIFY(!QFileInfo::exists(volumeFile(QStringLiteral("video/clip.mp4"))));

        // Restore the fixture for any later test relying on it.
        writeFile(volumeFile(QStringLiteral("video/clip.mp4")), QByteArray(4096, 'V'));
    }

    void renamesAnEntry()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        writeFile(volumeFile(QStringLiteral("vpk/old_name.vpk")), "x");

        QSignalSpy spy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.rename(QStringLiteral("/ux0:/vpk/old_name.vpk"),
                           QStringLiteral("/ux0:/vpk/new_name.vpk")) > 0);
        QVERIFY(spy.wait(5000));
        QCOMPARE(spy.first().at(1).toBool(), true);
        QVERIFY(!QFileInfo::exists(volumeFile(QStringLiteral("vpk/old_name.vpk"))));
        QVERIFY(QFileInfo::exists(volumeFile(QStringLiteral("vpk/new_name.vpk"))));
    }

    void reportsSizeOrFailsForAMissingFile()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        QSignalSpy sizeSpy(&usb, &RemoteTransport::sizeReady);
        QVERIFY(usb.requestSize(QStringLiteral("/ux0:/vpk/already.vpk")) > 0);
        QVERIFY(sizeSpy.wait(5000));
        QCOMPARE(sizeSpy.first().at(2).toLongLong(), qint64(128));

        QSignalSpy missingSpy(&usb, &RemoteTransport::commandFinished);
        QVERIFY(usb.requestSize(QStringLiteral("/ux0:/vpk/nope.vpk")) > 0);
        QVERIFY(missingSpy.wait(5000));
        QCOMPARE(missingSpy.first().at(1).toBool(), false);
    }

    void pingAnswersAsynchronously()
    {
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        QSignalSpy spy(&usb, &RemoteTransport::commandFinished);
        const int id = usb.ping();
        QVERIFY(id > 0);
        // Never inline: even a trivial request is answered from the event
        // loop, like every other RemoteTransport call.
        QCOMPARE(spy.count(), 0);
        QVERIFY(spy.wait(1000));
        QCOMPARE(spy.first().at(1).toBool(), true);
    }

    // --- fault isolation -----------------------------------------------

    void aMissingSubfolderFailsWithoutClaimingTheVolumeIsGone()
    {
        // Deleting a subfolder is not the same as the OS unmounting the
        // whole card -- QStorageInfo still resolves the (still mounted)
        // filesystem underneath, so this must surface as an ordinary
        // "not found" failure, not a connectionLost/disconnected cascade.
        UsbTransport usb;
        QVERIFY(attach(&usb, m_volumeRoot));

        const QString ghost = QDir(m_workRoot.path()).filePath(QStringLiteral("ghost_volume"));
        QDir().mkpath(QDir(ghost).filePath(QStringLiteral("app/VITASHELL")));
        writeFile(QDir(ghost).filePath(QStringLiteral("id.dat")), QByteArray(4, '\0'));
        QDir(ghost).mkpath(QStringLiteral("vpk"));

        UsbTransport ghostUsb;
        QVERIFY(attach(&ghostUsb, ghost));
        QVERIFY(QDir(ghost).filePath(QStringLiteral("vpk")).length() > 0);
        QVERIFY(QDir().rmdir(QDir(ghost).filePath(QStringLiteral("vpk"))));

        QSignalSpy connectionLostSpy(&ghostUsb, &RemoteTransport::connectionLost);
        QSignalSpy disconnectedSpy(&ghostUsb, &RemoteTransport::disconnected);
        QSignalSpy finishedSpy(&ghostUsb, &RemoteTransport::commandFinished);

        QVERIFY(ghostUsb.list(QStringLiteral("/ux0:/vpk")) > 0);
        QVERIFY(finishedSpy.wait(5000));

        QCOMPARE(finishedSpy.first().at(1).toBool(), false);
        QCOMPARE(connectionLostSpy.count(), 0);
        QCOMPARE(disconnectedSpy.count(), 0);
        QVERIFY(ghostUsb.isConnected());

        Q_UNUSED(usb)
    }
};

QTEST_MAIN(UsbTransportTest)
#include "tst_usb.moc"
