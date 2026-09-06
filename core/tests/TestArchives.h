#pragma once

// Fixture builders shared by the test suites: real SFO blobs, real ZIP
// archives and real theme.xml documents, so the parsers are exercised against
// the byte layouts they will meet rather than against mocks of themselves.

#include <QByteArray>
#include <QFile>
#include <QMap>
#include <QString>
#include <QVector>
#include <QtEndian>

#include <zlib.h>

namespace vsptest {


inline void appendLE32(QByteArray *out, quint32 value)
{
    char buffer[4];
    qToLittleEndian(value, buffer);
    out->append(buffer, 4);
}

inline void appendLE16(QByteArray *out, quint16 value)
{
    char buffer[2];
    qToLittleEndian(value, buffer);
    out->append(buffer, 2);
}

/// Builds a param.sfo containing the given string keys.
inline QByteArray makeSfo(const QMap<QString, QString> &values)
{
    QByteArray keyTable;
    QByteArray dataTable;
    QByteArray indexTable;

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        const quint16 keyOffset = static_cast<quint16>(keyTable.size());
        keyTable.append(it.key().toUtf8());
        keyTable.append('\0');

        const QByteArray value = it.value().toUtf8() + QByteArray(1, '\0');
        const quint32 dataOffset = static_cast<quint32>(dataTable.size());
        dataTable.append(value);

        appendLE16(&indexTable, keyOffset);
        appendLE16(&indexTable, 0x0204);                      // UTF-8, NUL-terminated
        appendLE32(&indexTable, static_cast<quint32>(value.size()));
        appendLE32(&indexTable, static_cast<quint32>(value.size()));
        appendLE32(&indexTable, dataOffset);
    }

    const quint32 headerSize = 20;
    const quint32 keyTableStart = headerSize + static_cast<quint32>(indexTable.size());
    const quint32 dataTableStart = keyTableStart + static_cast<quint32>(keyTable.size());

    QByteArray sfo;
    appendLE32(&sfo, 0x46535000);                             // "\0PSF"
    appendLE32(&sfo, 0x01010000);
    appendLE32(&sfo, keyTableStart);
    appendLE32(&sfo, dataTableStart);
    appendLE32(&sfo, static_cast<quint32>(values.size()));
    sfo.append(indexTable);
    sfo.append(keyTable);
    sfo.append(dataTable);
    return sfo;
}

inline QByteArray deflateRaw(const QByteArray &input)
{
    z_stream stream {};
    deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);

    QByteArray output;
    output.resize(static_cast<int>(deflateBound(&stream, static_cast<uLong>(input.size()))));

    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    deflate(&stream, Z_FINISH);
    output.resize(static_cast<int>(stream.total_out));
    deflateEnd(&stream);
    return output;
}

struct ZipMember
{
    QString name;
    QByteArray content;
    bool compress = false;
};

/// Minimal ZIP writer, only used to feed ZipReader something real to read.
inline QByteArray makeZip(const QVector<ZipMember> &members)
{
    QByteArray archive;
    QByteArray central;

    for (const ZipMember &member : members) {
        const QByteArray name = member.name.toUtf8();
        const QByteArray payload = member.compress ? deflateRaw(member.content) : member.content;
        const quint32 crc = crc32(0, reinterpret_cast<const Bytef *>(member.content.constData()),
                                  static_cast<uInt>(member.content.size()));
        const quint32 localOffset = static_cast<quint32>(archive.size());

        appendLE32(&archive, 0x04034b50);
        appendLE16(&archive, 20);
        appendLE16(&archive, 0);
        appendLE16(&archive, member.compress ? 8 : 0);
        appendLE16(&archive, 0);
        appendLE16(&archive, 0);
        appendLE32(&archive, crc);
        appendLE32(&archive, static_cast<quint32>(payload.size()));
        appendLE32(&archive, static_cast<quint32>(member.content.size()));
        appendLE16(&archive, static_cast<quint16>(name.size()));
        appendLE16(&archive, 0);
        archive.append(name);
        archive.append(payload);

        appendLE32(&central, 0x02014b50);
        appendLE16(&central, 20);
        appendLE16(&central, 20);
        appendLE16(&central, 0);
        appendLE16(&central, member.compress ? 8 : 0);
        appendLE16(&central, 0);
        appendLE16(&central, 0);
        appendLE32(&central, crc);
        appendLE32(&central, static_cast<quint32>(payload.size()));
        appendLE32(&central, static_cast<quint32>(member.content.size()));
        appendLE16(&central, static_cast<quint16>(name.size()));
        appendLE16(&central, 0);
        appendLE16(&central, 0);
        appendLE16(&central, 0);
        appendLE16(&central, 0);
        appendLE32(&central, 0);
        appendLE32(&central, localOffset);
        central.append(name);
    }

    const quint32 centralOffset = static_cast<quint32>(archive.size());
    archive.append(central);

    appendLE32(&archive, 0x06054b50);
    appendLE16(&archive, 0);
    appendLE16(&archive, 0);
    appendLE16(&archive, static_cast<quint16>(members.size()));
    appendLE16(&archive, static_cast<quint16>(members.size()));
    appendLE32(&archive, static_cast<quint32>(central.size()));
    appendLE32(&archive, centralOffset);
    appendLE16(&archive, 0);
    return archive;
}

/// Mirrors the layout of a real PS Vita theme.xml, including the language
/// blocks and the m_default element appearing under both m_title and
/// m_provider -- which is the case a naive parser gets wrong.
inline QByteArray makeThemeXml(const QString &title, const QString &provider)
{
    return QStringLiteral(R"(<?xml version="1.0" encoding="utf-8"?>
<theme format-ver="01.00" package="0">
	<HomeProperty>
		<m_bgParam>
			<BackgroundParam>
				<m_imageFilePath>br.png</m_imageFilePath>
				<m_thumbnailFilePath>tbr.png</m_thumbnailFilePath>
				<m_waveType>24</m_waveType>
				<m_fontColor>00D1FF</m_fontColor>
			</BackgroundParam>
		</m_bgParam>
	</HomeProperty>
	<InfomationBarProperty>
		<m_barColor>000000</m_barColor>
		<m_indicatorColor>FFFFFF</m_indicatorColor>
	</InfomationBarProperty>
	<InfomationProperty>
		<m_provider>
			<m_default>%2</m_default>
			<m_param>
				<m_ja>%2</m_ja>
				<m_fr>%2</m_fr>
			</m_param>
		</m_provider>
		<m_contentVer>01.00</m_contentVer>
		<m_title>
			<m_default>%1</m_default>
			<m_param>
				<m_ja>%1</m_ja>
				<m_fr>%1</m_fr>
			</m_param>
		</m_title>
		<m_homePreviewFilePath>preview_livearea.png</m_homePreviewFilePath>
		<m_startPreviewFilePath>preview_lockscreen.png</m_startPreviewFilePath>
		<m_packageImageFilePath>preview_thumbnail.png</m_packageImageFilePath>
	</InfomationProperty>
	<StartScreenProperty>
		<m_filePath>lockpaper.png</m_filePath>
	</StartScreenProperty>
</theme>)").arg(title, provider).toUtf8();
}

/// Writes an archive to a real file and returns its path.
inline QString writeArchive(const QString &path, const QVector<ZipMember> &members)
{
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(makeZip(members));
    out.close();
    return path;
}


} // namespace vsptest
