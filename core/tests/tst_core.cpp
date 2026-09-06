// Unit tests for the parts of the core that have to be right before a single
// byte reaches the device: input hygiene, package identification and the
// listing parser. Anything that needs a real Vita is out of scope here.

#include "TestArchives.h"

#include <vsp/ArchiveExtractor.h>
#include <vsp/FtpClient.h>
#include <vsp/PackageInspector.h>
#include <vsp/PathUtils.h>
#include <vsp/SfoReader.h>
#include <vsp/ThemeReader.h>
#include <vsp/VitaPaths.h>
#include <vsp/ZipReader.h>

#include <QDir>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>

#include <zlib.h>

using namespace vsp;

using namespace vsptest;


class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    // --- input hygiene ---------------------------------------------------

    void ipValidation_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("expected");

        QTest::newRow("typical")       << "192.168.1.40"   << true;
        QTest::newRow("zero")          << "0.0.0.0"        << true;
        QTest::newRow("broadcast")     << "255.255.255.255"<< true;
        QTest::newRow("spaces")        << "  10.0.0.1  "   << true;
        QTest::newRow("octet too big") << "192.168.1.256"  << false;
        QTest::newRow("three octets")  << "192.168.1"      << false;
        QTest::newRow("five octets")   << "1.2.3.4.5"      << false;
        QTest::newRow("leading zero")  << "192.168.01.1"   << false;
        QTest::newRow("letters")       << "192.168.a.1"    << false;
        QTest::newRow("empty octet")   << "192..1.1"       << false;
        QTest::newRow("hostname")      << "vita.local"     << false;
        QTest::newRow("empty")         << ""               << false;
    }

    void ipValidation()
    {
        QFETCH(QString, address);
        QFETCH(bool, expected);
        QCOMPARE(path::isValidIPv4(address), expected);
    }

    void fileNameSanitisation()
    {
        // Separators and traversal never survive.
        QVERIFY(!path::sanitizeFileName("../../etc/passwd").contains('/'));
        QVERIFY(!path::sanitizeFileName("..\\..\\windows").contains('\\'));
        QCOMPARE(path::sanitizeFileName(".."), QStringLiteral("unnamed"));
        QCOMPARE(path::sanitizeFileName("."), QStringLiteral("unnamed"));
        QCOMPARE(path::sanitizeFileName(""), QStringLiteral("unnamed"));

        // Control characters and shell-hostile glyphs are dropped.
        QCOMPARE(path::sanitizeFileName(QStringLiteral("na\x01me")), QStringLiteral("name"));
        QCOMPARE(path::sanitizeFileName(QStringLiteral("a<b>c")), QStringLiteral("abc"));

        // Ordinary names come through untouched.
        QCOMPARE(path::sanitizeFileName("Persona 4 Golden.vpk"),
                 QStringLiteral("Persona 4 Golden.vpk"));
    }

    void remoteNormalisation()
    {
        QCOMPARE(path::normalizeRemote("/ux0:/vpk/"), QStringLiteral("/ux0:/vpk"));
        QCOMPARE(path::normalizeRemote("ux0://vpk//x"), QStringLiteral("/ux0:/vpk/x"));
        QCOMPARE(path::normalizeRemote("/ux0:/vpk/./x"), QStringLiteral("/ux0:/vpk/x"));
        QCOMPARE(path::normalizeRemote("/ux0:/vpk/../app"), QStringLiteral("/ux0:/app"));

        // Escaping past the root is a no-op, never a path outside it.
        QCOMPARE(path::normalizeRemote("/../../.."), QStringLiteral("/"));
        QCOMPARE(path::normalizeRemote("/ux0:/../../.."), QStringLiteral("/"));
    }

    void remoteJoinRejectsTraversal()
    {
        // A malicious listing entry cannot climb out of the directory.
        const QString joined = path::joinRemote("/ux0:/vpk", "../../app/PCSE00001");
        QVERIFY(!joined.contains(QLatin1String("..")));
        QVERIFY(joined.startsWith(QLatin1String("/ux0:/vpk/")));
    }

    void localTargetStaysInsideChosenFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        const QString safe = path::safeLocalTarget(dir.path(), "save.bin");
        QVERIFY(safe.startsWith(QDir::cleanPath(dir.path())));

        // A remote server offering "../../evil" gets nothing outside the dir.
        const QString hostile = path::safeLocalTarget(dir.path(), "../../evil");
        QVERIFY(hostile.isEmpty() || hostile.startsWith(QDir::cleanPath(dir.path())));
        QVERIFY(!hostile.contains(QLatin1String("..")));
    }

    void protectedPathsAreTopLevelOnly()
    {
        QVERIFY(vitapaths::isProtectedPath("/ux0:"));
        QVERIFY(vitapaths::isProtectedPath("/ux0:/app"));
        QVERIFY(vitapaths::isProtectedPath("/ux0:/user"));
        // The user's own content inside those folders is not protected.
        QVERIFY(!vitapaths::isProtectedPath("/ux0:/app/PCSE00001"));
        QVERIFY(!vitapaths::isProtectedPath("/ux0:/vpk"));
    }

    void mediaRouting()
    {
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::Video), QStringLiteral("/ux0:/video"));
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::Photo), QStringLiteral("/ux0:/picture"));
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::Game), QStringLiteral("/ux0:/vpk"));
        // The two theme formats are unrelated and go to different places.
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::SystemTheme),
                 QStringLiteral("/ux0:/customtheme"));
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::ShellTheme),
                 QStringLiteral("/ux0:/VitaShell/theme"));
        // A different card routes to the same folders on that card.
        QCOMPARE(vitapaths::destinationDirFor(PackageKind::Video, "uma0:"),
                 QStringLiteral("/uma0:/video"));
    }

    void humanSizes()
    {
        QCOMPARE(path::humanSize(0), QStringLiteral("0 B"));
        QCOMPARE(path::humanSize(1023), QStringLiteral("1023 B"));
        QCOMPARE(path::humanSize(1024), QStringLiteral("1.0 KB"));
        QCOMPARE(path::humanSize(1024LL * 1024 * 1024), QStringLiteral("1.0 GB"));
    }

    void homeDirectoryCollapsesForDisplayAndExpandsBack()
    {
        // A folder field otherwise shows the OS account name verbatim to
        // anyone looking at (or screenshotting) the app on someone else's
        // machine.
        const QString home = QDir::homePath();

        QCOMPARE(path::collapseUserHome(home), QStringLiteral("~"));
        QCOMPARE(path::collapseUserHome(home + QStringLiteral("/Downloads/VitaSync")),
                 QStringLiteral("~/Downloads/VitaSync"));
        // Outside the home directory: unchanged, nothing to hide.
        QCOMPARE(path::collapseUserHome(QStringLiteral("/mnt/external/vita")),
                 QStringLiteral("/mnt/external/vita"));
        // A path that merely starts with the same characters as the home
        // directory, without being a real subpath of it, must not collapse.
        QCOMPARE(path::collapseUserHome(home + QStringLiteral("Extra")),
                 home + QStringLiteral("Extra"));

        // And the inverse actually resolves to the same real directory, so a
        // field showing "~/..." still works exactly as typing the full path
        // would -- this is display-only, never a stand-in the rest of the
        // app has to specially understand.
        QCOMPARE(path::expandUserHome(QStringLiteral("~")), home);
        QCOMPARE(path::expandUserHome(QStringLiteral("~/Downloads/VitaSync")),
                 home + QStringLiteral("/Downloads/VitaSync"));
        QCOMPARE(path::expandUserHome(QStringLiteral("/mnt/external/vita")),
                 QStringLiteral("/mnt/external/vita"));
    }

    // --- SFO --------------------------------------------------------------

    void sfoRoundTrip()
    {
        const QByteArray blob = makeSfo({
            { QStringLiteral("APP_VER"),  QStringLiteral("01.02") },
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Uncharted: Golden Abyss") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSE00001") }
        });

        SfoReader reader;
        QVERIFY2(reader.parse(blob), qPrintable(reader.errorString()));
        QCOMPARE(reader.titleId(), QStringLiteral("PCSE00001"));
        QCOMPARE(reader.title(), QStringLiteral("Uncharted: Golden Abyss"));
        QCOMPARE(reader.appVersion(), QStringLiteral("01.02"));
        QCOMPARE(reader.category(), QStringLiteral("gd"));
    }

    void sfoRejectsGarbage()
    {
        SfoReader reader;
        QVERIFY(!reader.parse(QByteArray()));
        QVERIFY(!reader.parse(QByteArray("not an sfo at all, really")));

        // A valid header whose offsets point past the end must be refused
        // rather than read out of bounds.
        QByteArray truncated = makeSfo({ { QStringLiteral("TITLE_ID"), QStringLiteral("PCSE00001") } });
        truncated.truncate(24);
        QVERIFY(!reader.parse(truncated));
    }

    // --- ZIP --------------------------------------------------------------

    void zipReadsStoredAndDeflated()
    {
        const QByteArray sfo = makeSfo({ { QStringLiteral("TITLE_ID"), QStringLiteral("PCSB00245") } });
        const QByteArray icon = QByteArray(4096, '\x7f');

        const QByteArray archive = makeZip({
            { QStringLiteral("eboot.bin"), QByteArray(256, 'E'), false },
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("sce_sys/icon0.png"), icon, true }
        });

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("test.vpk"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(archive);
        out.close();

        ZipReader reader;
        QVERIFY2(reader.open(file), qPrintable(reader.errorString()));
        QCOMPARE(reader.entryNames().size(), 3);
        QCOMPARE(reader.read(QStringLiteral("sce_sys/param.sfo")), sfo);
        QCOMPARE(reader.read(QStringLiteral("sce_sys/icon0.png")), icon);
        QVERIFY(reader.findEntryInsensitive(QStringLiteral("EBOOT.BIN")) != nullptr);
    }

    void zipRefusesNonArchive()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("bogus.vpk"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QByteArray(5000, 'x'));
        out.close();

        ZipReader reader;
        QVERIFY(!reader.open(file));
        QVERIFY(!reader.errorString().isEmpty());
    }

    void zipReadRespectsSizeLimit()
    {
        const QByteArray big(200000, 'B');
        const QByteArray archive = makeZip({ { QStringLiteral("big.bin"), big, false } });

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("big.zip"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(archive);
        out.close();

        ZipReader reader;
        QVERIFY(reader.open(file));
        QVERIFY(reader.read(QStringLiteral("big.bin"), 1024).isEmpty());
        QCOMPARE(reader.read(QStringLiteral("big.bin"), 1 << 20).size(), big.size());
    }

    // --- package identification ------------------------------------------

    void inspectIdentifiesGameFromSfo()
    {
        const QByteArray sfo = makeSfo({
            { QStringLiteral("APP_VER"),  QStringLiteral("01.00") },
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Gravity Rush") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSA00011") }
        });
        const QByteArray archive = makeZip({
            { QStringLiteral("eboot.bin"), QByteArray(64, 'E'), false },
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            { QStringLiteral("sce_sys/icon0.png"), QByteArray(128, 'I'), false }
        });

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("gravity.vpk"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(archive);
        out.close();

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::Game);
        QCOMPARE(info.titleId, QStringLiteral("PCSA00011"));
        QCOMPARE(info.title, QStringLiteral("Gravity Rush"));
        QCOMPARE(info.appVersion, QStringLiteral("01.00"));
        QCOMPARE(info.iconPng.size(), 128);
        QVERIFY(info.isInstallable());
    }

    void inspectClassifiesHomebrewSeparately()
    {
        // A made-up Title ID is homebrew, not a retail game.
        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("VitaShell") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("VITASHELL") }
        });
        const QByteArray archive = makeZip({
            { QStringLiteral("sce_sys/param.sfo"), sfo, false }
        });

        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("shell.vpk"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(archive);
        out.close();

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY(info.valid);
        QCOMPARE(info.kind, PackageKind::Homebrew);
        QCOMPARE(info.title, QStringLiteral("VitaShell"));
    }

    void inspectClassifiesMediaByExtension()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("holiday.mp4"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(QByteArray(1024, 'm'));
        out.close();

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY(info.valid);
        QCOMPARE(info.kind, PackageKind::Video);
        QVERIFY(!info.isInstallable());
    }

    void sha256MatchesKnownVector()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("abc.bin"));
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write("abc");
        out.close();

        QCOMPARE(PackageInspector::sha256OfFile(file),
                 QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    }

    // --- themes -----------------------------------------------------------

    void themeXmlParsingPicksTheRightDefaults()
    {
        const ThemeInfo info = ThemeReader::parseThemeXml(
            makeThemeXml(QStringLiteral("Hu-Tao"), QStringLiteral("CTRPlugin")));

        QVERIFY2(info.valid, qPrintable(info.error));
        // m_default appears under m_title and m_provider; they must not swap.
        QCOMPARE(info.title, QStringLiteral("Hu-Tao"));
        QCOMPARE(info.provider, QStringLiteral("CTRPlugin"));
        QCOMPARE(info.contentVersion, QStringLiteral("01.00"));
        QCOMPARE(info.formatVersion, QStringLiteral("01.00"));
        QCOMPARE(info.previewFile, QStringLiteral("preview_thumbnail.png"));
    }

    void themeXmlRejectsMalformedInput()
    {
        QVERIFY(!ThemeReader::parseThemeXml(QByteArray()).valid);
        QVERIFY(!ThemeReader::parseThemeXml("<theme><unclosed>").valid);
        // Well-formed XML that is not a theme is refused rather than accepted
        // with empty fields.
        QVERIFY(!ThemeReader::parseThemeXml("<?xml version=\"1.0\"?><notatheme/>").valid);
    }

    void inspectIdentifiesSystemThemeInAFolder()
    {
        QTemporaryDir dir;
        const QString file = writeArchive(dir.filePath(QStringLiteral("HuTao.zip")), {
            { QStringLiteral("Hu-Tao_Theme/theme.xml"),
              makeThemeXml(QStringLiteral("Hu-Tao"), QStringLiteral("CTRPlugin")), true },
            { QStringLiteral("Hu-Tao_Theme/br.png"), QByteArray(2048, 'B'), false },
            { QStringLiteral("Hu-Tao_Theme/preview_thumbnail.png"), QByteArray(512, 'T'), false },
            { QStringLiteral("Hu-Tao_Theme/icon_settings.png"), QByteArray(256, 'I'), false }
        });
        QVERIFY(!file.isEmpty());

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::SystemTheme);
        QCOMPARE(info.title, QStringLiteral("Hu-Tao"));
        QCOMPARE(info.provider, QStringLiteral("CTRPlugin"));
        // The folder name inside the archive is the one it must land in.
        QCOMPARE(info.themeFolderName, QStringLiteral("Hu-Tao_Theme"));
        QCOMPARE(info.archivePrefix, QStringLiteral("Hu-Tao_Theme/"));
        QCOMPARE(info.entryCount, 4);
        QVERIFY(info.isDirectoryPayload());
        // The declared thumbnail is pulled out so the card shows real art.
        QCOMPARE(info.iconPng.size(), 512);
    }

    void inspectIdentifiesSystemThemeAtArchiveRoot()
    {
        QTemporaryDir dir;
        const QString file = writeArchive(dir.filePath(QStringLiteral("Flat Theme.zip")), {
            { QStringLiteral("theme.xml"),
              makeThemeXml(QStringLiteral("Flat"), QStringLiteral("Someone")), false },
            { QStringLiteral("br.png"), QByteArray(64, 'B'), false }
        });

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY(info.valid);
        QCOMPARE(info.kind, PackageKind::SystemTheme);
        QVERIFY(info.archivePrefix.isEmpty());
        // With no folder in the archive, the archive's own name is used.
        QCOMPARE(info.themeFolderName, QStringLiteral("Flat Theme"));
    }

    void inspectIdentifiesShellTheme()
    {
        QTemporaryDir dir;
        const QString file = writeArchive(dir.filePath(QStringLiteral("Midnight.zip")), {
            { QStringLiteral("Midnight/colors.txt"),
              QByteArray("BACKGROUND_COLOR = 0x00000000\n"), false },
            { QStringLiteral("Midnight/bg_browser.png"), QByteArray(1024, 'W'), false },
            { QStringLiteral("Midnight/folder_icon.png"), QByteArray(128, 'F'), false },
            { QStringLiteral("Midnight/wallpaper.png"), QByteArray(2048, 'P'), false }
        });

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY2(info.valid, qPrintable(info.error));
        QCOMPARE(info.kind, PackageKind::ShellTheme);
        QCOMPARE(info.themeFolderName, QStringLiteral("Midnight"));
        QVERIFY(info.isDirectoryPayload());
        // A shell skin has no manifest, so the wallpaper stands in as preview.
        QCOMPARE(info.iconPng.size(), 2048);
    }

    void inspectDoesNotMistakeAGameForATheme()
    {
        QTemporaryDir dir;
        const QByteArray sfo = makeSfo({
            { QStringLiteral("CATEGORY"), QStringLiteral("gd") },
            { QStringLiteral("TITLE"),    QStringLiteral("Gravity Rush") },
            { QStringLiteral("TITLE_ID"), QStringLiteral("PCSA00011") }
        });
        const QString file = writeArchive(dir.filePath(QStringLiteral("g.vpk")), {
            { QStringLiteral("sce_sys/param.sfo"), sfo, false },
            // A game may legitimately ship an icon called cover.png.
            { QStringLiteral("sce_sys/livearea/contents/cover.png"), QByteArray(64, 'C'), false },
            { QStringLiteral("eboot.bin"), QByteArray(64, 'E'), false }
        });

        const PackageInfo info = PackageInspector::inspect(file);
        QCOMPARE(info.kind, PackageKind::Game);
        QVERIFY(!info.isDirectoryPayload());
    }

    void looseImagesAreNotAShellTheme()
    {
        // Two stray files that happen to share a name with the VitaShell set
        // must not be promoted to a theme.
        QTemporaryDir dir;
        const QString file = writeArchive(dir.filePath(QStringLiteral("pics.zip")), {
            { QStringLiteral("play.png"), QByteArray(32, 'A'), false },
            { QStringLiteral("pause.png"), QByteArray(32, 'B'), false }
        });

        const PackageInfo info = PackageInspector::inspect(file);
        QVERIFY(info.kind != PackageKind::ShellTheme);
        QVERIFY(info.kind != PackageKind::SystemTheme);
    }

    // --- extraction safety ------------------------------------------------

    void extractorRefusesToEscapeTheDestination()
    {
        QTemporaryDir work;
        const QString archivePath = writeArchive(work.filePath(QStringLiteral("evil.zip")), {
            { QStringLiteral("../escape.txt"), QByteArray("no"), false },
            { QStringLiteral("../../deeper.txt"), QByteArray("no"), false },
            { QStringLiteral("/absolute.txt"), QByteArray("no"), false },
            { QStringLiteral("sub/../../sneaky.txt"), QByteArray("no"), false },
            { QStringLiteral("good.txt"), QByteArray("yes"), false }
        });

        ZipReader archive;
        QVERIFY(archive.open(archivePath));

        const QString destination = work.filePath(QStringLiteral("out"));
        const ExtractionResult result =
            ArchiveExtractor::extractSubtree(&archive, QString(), destination);

        QVERIFY2(result.ok, qPrintable(result.error));

        // The property that matters is that nothing landed outside the
        // destination -- not how many entries survived. Traversal entries are
        // refused outright; an absolute name is stripped to its leaf and kept
        // inside, which is safe and does not throw away a usable file.
        const QString root = QDir::cleanPath(destination);
        for (const ExtractedFile &file : result.files) {
            QVERIFY2(file.localPath.startsWith(root + QLatin1Char('/')),
                     qPrintable(QStringLiteral("escaped to %1").arg(file.localPath)));
            QVERIFY(!file.relativePath.contains(QLatin1String("..")));
        }

        QStringList relatives;
        for (const ExtractedFile &file : result.files)
            relatives << file.relativePath;
        std::sort(relatives.begin(), relatives.end());
        QCOMPARE(relatives, QStringList({ QStringLiteral("absolute.txt"),
                                          QStringLiteral("good.txt") }));

        // The three traversal entries were refused, and nothing was written to
        // the parent directory they were aiming at.
        QCOMPARE(result.skipped, 3);
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral("escape.txt"))));
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral("deeper.txt"))));
        QVERIFY(!QFileInfo::exists(work.filePath(QStringLiteral("sneaky.txt"))));
    }

    void extractorPreservesNestedStructure()
    {
        QTemporaryDir work;
        const QString archivePath = writeArchive(work.filePath(QStringLiteral("t.zip")), {
            { QStringLiteral("Theme/theme.xml"), QByteArray("<theme/>"), false },
            { QStringLiteral("Theme/img/bg.png"), QByteArray(100, 'X'), true },
            { QStringLiteral("Theme/img/deep/tiny.png"), QByteArray(10, 'Y'), false },
            { QStringLiteral("Other/ignored.txt"), QByteArray("no"), false }
        });

        ZipReader archive;
        QVERIFY(archive.open(archivePath));

        const QString destination = work.filePath(QStringLiteral("out"));
        const ExtractionResult result =
            ArchiveExtractor::extractSubtree(&archive, QStringLiteral("Theme/"), destination);

        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.files.size(), 3);          // the Other/ entry is outside the prefix

        QStringList relatives;
        for (const ExtractedFile &file : result.files)
            relatives << file.relativePath;
        std::sort(relatives.begin(), relatives.end());
        QCOMPARE(relatives, QStringList({ QStringLiteral("img/bg.png"),
                                          QStringLiteral("img/deep/tiny.png"),
                                          QStringLiteral("theme.xml") }));
        QVERIFY(QFileInfo::exists(destination + QStringLiteral("/img/deep/tiny.png")));
    }

    void extractorEnforcesItsLimits()
    {
        QTemporaryDir work;
        const QString archivePath = writeArchive(work.filePath(QStringLiteral("big.zip")), {
            { QStringLiteral("a.bin"), QByteArray(200000, 'A'), false },
            { QStringLiteral("b.bin"), QByteArray(200000, 'B'), false }
        });

        ZipReader archive;
        QVERIFY(archive.open(archivePath));

        ExtractionLimits limits;
        limits.maxTotalBytes = 250000;
        const ExtractionResult result = ArchiveExtractor::extractSubtree(
            &archive, QString(), work.filePath(QStringLiteral("out")), limits);

        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }

    // --- listing parser ---------------------------------------------------

    void listingParsesUnixFormat()
    {
        const QByteArray listing =
            "drwxrwxrwx 1 vita vita     0 Jan 01 1970 vpk\r\n"
            "-rw-rw-rw- 1 vita vita 92841 Mar 14 21:07 Gravity Rush.vpk\r\n"
            "drwxrwxrwx 1 vita vita     0 Jan 01 1970 .\r\n"
            "drwxrwxrwx 1 vita vita     0 Jan 01 1970 ..\r\n";

        const QVector<RemoteEntry> entries = FtpClient::parseListing(listing, "/ux0:");
        QCOMPARE(entries.size(), 2);

        // Directories sort first.
        QCOMPARE(entries.at(0).name, QStringLiteral("vpk"));
        QVERIFY(entries.at(0).isDirectory);
        QCOMPARE(entries.at(0).path, QStringLiteral("/ux0:/vpk"));

        // Names with spaces survive intact, and sizes are read.
        QCOMPARE(entries.at(1).name, QStringLiteral("Gravity Rush.vpk"));
        QVERIFY(!entries.at(1).isDirectory);
        QCOMPARE(entries.at(1).size, 92841LL);
    }

    void listingDropsHostileNames()
    {
        // A server that puts a separator in a name is trying something; the
        // entry is discarded rather than sanitised into a different file.
        const QByteArray listing =
            "-rw-rw-rw- 1 vita vita 10 Jan 01 1970 ../../escape.bin\r\n"
            "-rw-rw-rw- 1 vita vita 10 Jan 01 1970 fine.bin\r\n";

        const QVector<RemoteEntry> entries = FtpClient::parseListing(listing, "/ux0:");
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.at(0).name, QStringLiteral("fine.bin"));
    }

    void listingIgnoresJunkLines()
    {
        const QByteArray listing = "total 4\r\ngarbage\r\n\r\n";
        QCOMPARE(FtpClient::parseListing(listing, "/ux0:").size(), 0);
    }
};

QTEST_MAIN(CoreTest)
#include "tst_core.moc"
