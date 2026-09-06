#include "ThemeReader.h"

#include "ZipReader.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>

namespace vsp {
namespace {

// Deepest a theme folder may sit inside an archive before we stop believing it
// is a theme rather than a backup of somebody's memory card.
constexpr int kMaxThemeDepth = 3;

QString folderNameFromPrefix(const QString &prefix, const QString &fallback)
{
    const QString trimmed = prefix.endsWith(QLatin1Char('/')) ? prefix.chopped(1) : prefix;
    if (trimmed.isEmpty())
        return fallback;
    const int slash = trimmed.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? trimmed : trimmed.mid(slash + 1);
}

/// Finds the shallowest entry whose file name is \a marker, and returns the
/// path prefix it sits under. Returns false when there is no such entry.
bool findMarker(const QStringList &names, const QString &marker, QString *prefix)
{
    int bestDepth = kMaxThemeDepth + 1;
    bool found = false;

    for (const QString &name : names) {
        const int slash = name.lastIndexOf(QLatin1Char('/'));
        const QString leaf = slash < 0 ? name : name.mid(slash + 1);
        if (leaf.compare(marker, Qt::CaseInsensitive) != 0)
            continue;

        const QString candidate = slash < 0 ? QString() : name.left(slash + 1);
        const int depth = static_cast<int>(candidate.count(QLatin1Char('/')));
        if (depth < bestDepth) {
            bestDepth = depth;
            *prefix = candidate;
            found = true;
        }
    }
    return found;
}

/// Accepts the "RRGGBB" and "AARRGGBB" forms the two theme formats use and
/// normalises them to "#RRGGBB". Duplicates and unreadable values are dropped.
void appendColor(QStringList *colors, const QString &raw)
{
    if (!colors || colors->size() >= 4)
        return;

    QString hex = raw.trimmed();
    if (hex.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        hex.remove(0, 2);
    hex.remove(QLatin1Char('#'));

    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f]+$"));
    if (!hexOnly.match(hex).hasMatch())
        return;

    if (hex.size() == 8)
        hex = hex.right(6);          // drop the alpha byte
    if (hex.size() != 6)
        return;

    const QString normalised = QLatin1Char('#') + hex.toUpper();
    if (normalised == QLatin1String("#000000") || colors->contains(normalised))
        return;
    colors->append(normalised);
}

} // namespace

QString ThemeInfo::displayName() const
{
    if (!title.isEmpty())
        return title;
    return folderName;
}

const QStringList &ThemeReader::shellThemeFileNames()
{
    // Straight from VitaShell's documented customisation table.
    static const QStringList names = {
        QStringLiteral("colors.txt"),         QStringLiteral("archive_icon.png"),
        QStringLiteral("audio_icon.png"),     QStringLiteral("battery.png"),
        QStringLiteral("battery_bar_charge.png"), QStringLiteral("battery_bar_green.png"),
        QStringLiteral("battery_bar_red.png"), QStringLiteral("bg_audioplayer.png"),
        QStringLiteral("bg_browser.png"),     QStringLiteral("bg_hexeditor.png"),
        QStringLiteral("bg_photoviewer.png"), QStringLiteral("bg_texteditor.png"),
        QStringLiteral("context.png"),        QStringLiteral("context_more.png"),
        QStringLiteral("cover.png"),          QStringLiteral("dialog.png"),
        QStringLiteral("fastforward.png"),    QStringLiteral("fastrewind.png"),
        QStringLiteral("file_icon.png"),      QStringLiteral("folder_icon.png"),
        QStringLiteral("ftp.png"),            QStringLiteral("image_icon.png"),
        QStringLiteral("pause.png"),          QStringLiteral("play.png"),
        QStringLiteral("settings.png"),       QStringLiteral("sfo_icon.png"),
        QStringLiteral("text_icon.png"),      QStringLiteral("wallpaper.png"),
        QStringLiteral("font.pgf")
    };
    return names;
}

ThemeInfo ThemeReader::parseThemeXml(const QByteArray &xml)
{
    ThemeInfo info;
    info.flavour = ThemeFlavour::SystemTheme;

    QXmlStreamReader reader(xml);
    QStringList path;
    bool sawThemeRoot = false;

    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::StartElement) {
            path.append(reader.name().toString());

            if (path.size() == 1 && path.first() == QLatin1String("theme")) {
                sawThemeRoot = true;
                info.formatVersion =
                    reader.attributes().value(QLatin1String("format-ver")).toString();
            }
            continue;
        }

        if (token == QXmlStreamReader::EndElement) {
            if (!path.isEmpty())
                path.removeLast();
            continue;
        }

        if (token != QXmlStreamReader::Characters || reader.isWhitespace())
            continue;

        // m_default appears under both m_title and m_provider, so the whole
        // element path decides which field this text belongs to.
        const QString here = path.join(QLatin1Char('/'));
        const QString text = reader.text().toString().trimmed();
        if (text.isEmpty())
            continue;

        if (here == QLatin1String("theme/InfomationProperty/m_title/m_default"))
            info.title = text;
        else if (here == QLatin1String("theme/InfomationProperty/m_provider/m_default"))
            info.provider = text;
        else if (here == QLatin1String("theme/InfomationProperty/m_contentVer"))
            info.contentVersion = text;
        else if (here == QLatin1String("theme/InfomationProperty/m_packageImageFilePath"))
            info.previewFile = text;
        else if (info.previewFile.isEmpty()
                 && here == QLatin1String("theme/InfomationProperty/m_homePreviewFilePath"))
            info.previewFile = text;
        else if (here.endsWith(QLatin1String("m_barColor"))
                 || here.endsWith(QLatin1String("m_fontColor"))
                 || here.endsWith(QLatin1String("m_dateColor"))
                 || here.endsWith(QLatin1String("m_indicatorColor")))
            appendColor(&info.accentColors, text);
    }

    if (reader.hasError()) {
        info.error = QStringLiteral("theme.xml is not well-formed: %1").arg(reader.errorString());
        return info;
    }
    if (!sawThemeRoot) {
        info.error = QStringLiteral("theme.xml has no <theme> root element");
        return info;
    }

    info.valid = true;
    return info;
}

ThemeInfo ThemeReader::inspectArchive(ZipReader *archive, const QString &fallbackName)
{
    ThemeInfo info;
    if (!archive || !archive->isOpen())
        return info;

    const QStringList names = archive->entryNames();

    // A system theme is authoritative: theme.xml is unambiguous.
    QString prefix;
    if (findMarker(names, QStringLiteral("theme.xml"), &prefix)) {
        const QByteArray xml = archive->read(prefix + QStringLiteral("theme.xml"),
                                             2 * 1024 * 1024);
        info = xml.isEmpty() ? ThemeInfo() : parseThemeXml(xml);
        info.flavour = ThemeFlavour::SystemTheme;
        info.archivePrefix = prefix;
        info.folderName = folderNameFromPrefix(prefix, fallbackName);

        if (xml.isEmpty()) {
            info.error = QStringLiteral("theme.xml could not be read from the archive");
            return info;
        }

        if (!info.previewFile.isEmpty()) {
            info.previewPng = archive->read(prefix + info.previewFile, 8 * 1024 * 1024);
        }
        // Fall back to any thumbnail-looking image so the card is never blank.
        if (info.previewPng.isEmpty()) {
            for (const QString &candidate : { QStringLiteral("preview_thumbnail.png"),
                                              QStringLiteral("preview_livearea.png"),
                                              QStringLiteral("preview_lockscreen.png") }) {
                info.previewPng = archive->read(prefix + candidate, 8 * 1024 * 1024);
                if (!info.previewPng.isEmpty())
                    break;
            }
        }
    } else {
        // A shell theme has no manifest, so it is recognised by its contents.
        QString colorsPrefix;
        const bool hasColors = findMarker(names, QStringLiteral("colors.txt"), &colorsPrefix);

        const QStringList &known = shellThemeFileNames();
        QHash<QString, int> hitsByPrefix;
        for (const QString &name : names) {
            const int slash = name.lastIndexOf(QLatin1Char('/'));
            const QString leaf = slash < 0 ? name : name.mid(slash + 1);
            const QString entryPrefix = slash < 0 ? QString() : name.left(slash + 1);
            if (entryPrefix.count(QLatin1Char('/')) > kMaxThemeDepth)
                continue;
            if (known.contains(leaf, Qt::CaseInsensitive))
                ++hitsByPrefix[entryPrefix];
        }

        QString bestPrefix;
        int bestHits = 0;
        for (auto it = hitsByPrefix.constBegin(); it != hitsByPrefix.constEnd(); ++it) {
            if (it.value() > bestHits) {
                bestHits = it.value();
                bestPrefix = it.key();
            }
        }

        // colors.txt alone is decisive; otherwise several documented files have
        // to line up before we call a folder of PNGs a VitaShell skin.
        if (!hasColors && bestHits < 3)
            return info;

        info.flavour = ThemeFlavour::ShellTheme;
        info.archivePrefix = hasColors ? colorsPrefix : bestPrefix;

        if (hasColors) {
            // colors.txt is "NAME = 0xAARRGGBB" per line.
            const QByteArray palette =
                archive->read(colorsPrefix + QStringLiteral("colors.txt"), 256 * 1024);
            const QList<QByteArray> lines = palette.split('\n');
            for (const QByteArray &line : lines) {
                const int equals = line.indexOf('=');
                if (equals < 0)
                    continue;
                appendColor(&info.accentColors,
                            QString::fromLatin1(line.mid(equals + 1)).trimmed());
            }
        }
        info.folderName = folderNameFromPrefix(info.archivePrefix, fallbackName);
        info.valid = true;

        for (const QString &candidate : { QStringLiteral("wallpaper.png"),
                                          QStringLiteral("bg_browser.png"),
                                          QStringLiteral("cover.png") }) {
            info.previewPng = archive->read(info.archivePrefix + candidate, 8 * 1024 * 1024);
            if (!info.previewPng.isEmpty()) {
                info.previewFile = candidate;
                break;
            }
        }
    }

    // Count only what actually belongs to the theme.
    for (const QString &name : names) {
        if (!name.startsWith(info.archivePrefix, Qt::CaseInsensitive))
            continue;
        if (name.endsWith(QLatin1Char('/')))
            continue;
        const ZipReader::Entry *entry = archive->entry(name);
        if (!entry)
            continue;
        ++info.fileCount;
        info.unpackedSize += static_cast<qint64>(entry->uncompressedSize);
    }

    if (info.fileCount == 0) {
        info.valid = false;
        info.error = QStringLiteral("The theme folder in this archive is empty");
    }

    return info;
}

} // namespace vsp
