#include "ZipReader.h"

#include <QtEndian>

#include <zlib.h>

namespace vsp {
namespace {

constexpr quint32 kSigLocalHeader     = 0x04034b50u;
constexpr quint32 kSigCentralHeader   = 0x02014b50u;
constexpr quint32 kSigEndOfCentralDir = 0x06054b50u;
constexpr quint32 kSigZip64Locator    = 0x07064b50u;
constexpr quint32 kSigZip64EndOfCd    = 0x06064b50u;

constexpr quint16 kMethodStore   = 0;
constexpr quint16 kMethodDeflate = 8;

constexpr quint32 kZip64Marker32 = 0xffffffffu;
constexpr quint16 kZip64Marker16 = 0xffffu;

template <typename T>
T le(const QByteArray &buffer, int offset)
{
    if (offset < 0 || offset + static_cast<int>(sizeof(T)) > buffer.size())
        return T{};
    return qFromLittleEndian<T>(reinterpret_cast<const uchar *>(buffer.constData()) + offset);
}

QByteArray inflateRaw(const QByteArray &input, quint64 expectedSize, qint64 maxSize)
{
    if (expectedSize > static_cast<quint64>(maxSize))
        return {};

    QByteArray output;
    output.resize(static_cast<int>(expectedSize));

    z_stream stream {};
    // Negative window bits selects a raw deflate stream (no zlib/gzip wrapper),
    // which is exactly what a ZIP member holds.
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
        return {};

    stream.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.constData()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef *>(output.data());
    stream.avail_out = static_cast<uInt>(output.size());

    const int result = inflate(&stream, Z_FINISH);
    const uLong produced = stream.total_out;
    inflateEnd(&stream);

    if (result != Z_STREAM_END && result != Z_OK && result != Z_BUF_ERROR)
        return {};

    output.resize(static_cast<int>(produced));
    return output;
}

} // namespace

ZipReader::ZipReader(const QString &filePath)
{
    open(filePath);
}

ZipReader::~ZipReader()
{
    close();
}

bool ZipReader::open(const QString &filePath)
{
    close();

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        m_error = m_file.errorString();
        return false;
    }
    if (!buildIndex()) {
        m_file.close();
        return false;
    }
    m_indexed = true;
    return true;
}

void ZipReader::close()
{
    if (m_file.isOpen())
        m_file.close();
    m_entries.clear();
    m_order.clear();
    m_indexed = false;
    m_error.clear();
}

QStringList ZipReader::entryNames() const
{
    return m_order;
}

bool ZipReader::contains(const QString &name) const
{
    return m_entries.contains(name);
}

const ZipReader::Entry *ZipReader::entry(const QString &name) const
{
    const auto it = m_entries.constFind(name);
    return it == m_entries.constEnd() ? nullptr : &it.value();
}

const ZipReader::Entry *ZipReader::findEntryInsensitive(const QString &name) const
{
    if (const Entry *exact = entry(name))
        return exact;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (it.key().compare(name, Qt::CaseInsensitive) == 0)
            return &it.value();
    }
    return nullptr;
}

qint64 ZipReader::findEndOfCentralDirectory()
{
    // The EOCD record sits at the very end, possibly behind up to 64 KiB of
    // archive comment, so scan backwards over that window only.
    const qint64 fileSize = m_file.size();
    constexpr qint64 kMinEocd = 22;
    if (fileSize < kMinEocd)
        return -1;

    const qint64 windowSize = qMin<qint64>(fileSize, 64 * 1024 + kMinEocd);
    const qint64 windowStart = fileSize - windowSize;
    if (!m_file.seek(windowStart))
        return -1;

    const QByteArray window = m_file.read(windowSize);
    for (qint64 i = window.size() - kMinEocd; i >= 0; --i) {
        if (le<quint32>(window, static_cast<int>(i)) == kSigEndOfCentralDir)
            return windowStart + i;
    }
    return -1;
}

bool ZipReader::buildIndex()
{
    const qint64 eocdOffset = findEndOfCentralDirectory();
    if (eocdOffset < 0) {
        m_error = QStringLiteral("Not a ZIP archive (no end-of-central-directory record)");
        return false;
    }

    if (!m_file.seek(eocdOffset))
        return false;
    const QByteArray eocd = m_file.read(22);
    if (eocd.size() < 22) {
        m_error = QStringLiteral("Archive directory is truncated");
        return false;
    }

    quint64 entryCount = le<quint16>(eocd, 10);
    quint64 centralDirOffset = le<quint32>(eocd, 16);

    // ZIP64: the 32-bit fields saturate and the real values live in a separate
    // record pointed at by a locator immediately before the EOCD.
    if (entryCount == kZip64Marker16 || centralDirOffset == kZip64Marker32) {
        const qint64 locatorOffset = eocdOffset - 20;
        if (locatorOffset >= 0 && m_file.seek(locatorOffset)) {
            const QByteArray locator = m_file.read(20);
            if (locator.size() == 20 && le<quint32>(locator, 0) == kSigZip64Locator) {
                const quint64 zip64Offset = le<quint64>(locator, 8);
                if (zip64Offset < static_cast<quint64>(m_file.size()) && m_file.seek(zip64Offset)) {
                    const QByteArray zip64 = m_file.read(56);
                    if (zip64.size() >= 56 && le<quint32>(zip64, 0) == kSigZip64EndOfCd) {
                        entryCount = le<quint64>(zip64, 32);
                        centralDirOffset = le<quint64>(zip64, 48);
                    }
                }
            }
        }
    }

    if (centralDirOffset >= static_cast<quint64>(m_file.size())) {
        m_error = QStringLiteral("Archive directory offset is out of range");
        return false;
    }
    if (entryCount > 200000) {
        m_error = QStringLiteral("Archive declares an implausible number of entries");
        return false;
    }

    return readCentralDirectory(static_cast<qint64>(centralDirOffset), entryCount);
}

void ZipReader::applyZip64Extra(Entry *entry, const QByteArray &extra,
                                bool needUncompressed, bool needCompressed, bool needOffset)
{
    int cursor = 0;
    while (cursor + 4 <= extra.size()) {
        const quint16 headerId = le<quint16>(extra, cursor);
        const quint16 dataSize = le<quint16>(extra, cursor + 2);
        const int dataStart = cursor + 4;
        if (dataStart + dataSize > extra.size())
            return;

        if (headerId == 0x0001) {                // ZIP64 extended information
            int field = dataStart;
            // Fields appear only for the values that saturated, in this order.
            if (needUncompressed && field + 8 <= dataStart + dataSize) {
                entry->uncompressedSize = le<quint64>(extra, field);
                field += 8;
            }
            if (needCompressed && field + 8 <= dataStart + dataSize) {
                entry->compressedSize = le<quint64>(extra, field);
                field += 8;
            }
            if (needOffset && field + 8 <= dataStart + dataSize)
                entry->localHeaderOffset = le<quint64>(extra, field);
            return;
        }
        cursor = dataStart + dataSize;
    }
}

bool ZipReader::readCentralDirectory(qint64 offset, quint64 entryCount)
{
    if (!m_file.seek(offset))
        return false;

    for (quint64 i = 0; i < entryCount; ++i) {
        const QByteArray header = m_file.read(46);
        if (header.size() < 46 || le<quint32>(header, 0) != kSigCentralHeader) {
            // A short directory still gives us whatever we already indexed.
            break;
        }

        Entry entry;
        entry.compressionMethod = le<quint16>(header, 10);
        entry.crc32             = le<quint32>(header, 16);
        entry.compressedSize    = le<quint32>(header, 20);
        entry.uncompressedSize  = le<quint32>(header, 24);
        entry.localHeaderOffset = le<quint32>(header, 42);

        const quint16 nameLength    = le<quint16>(header, 28);
        const quint16 extraLength   = le<quint16>(header, 30);
        const quint16 commentLength = le<quint16>(header, 32);

        const QByteArray nameBytes = m_file.read(nameLength);
        const QByteArray extra     = m_file.read(extraLength);
        m_file.skip(commentLength);

        if (nameBytes.size() != nameLength)
            break;

        applyZip64Extra(&entry, extra,
                        entry.uncompressedSize == kZip64Marker32,
                        entry.compressedSize == kZip64Marker32,
                        entry.localHeaderOffset == kZip64Marker32);

        entry.name = QString::fromUtf8(nameBytes).replace(QLatin1Char('\\'), QLatin1Char('/'));
        entry.isDirectory = entry.name.endsWith(QLatin1Char('/'));

        if (entry.name.isEmpty())
            continue;
        if (!m_entries.contains(entry.name))
            m_order.append(entry.name);
        m_entries.insert(entry.name, entry);
    }

    if (m_entries.isEmpty()) {
        m_error = QStringLiteral("Archive contains no readable entries");
        return false;
    }
    return true;
}

QByteArray ZipReader::read(const QString &name, qint64 maxSize)
{
    const Entry *found = findEntryInsensitive(name);
    if (!found) {
        m_error = QStringLiteral("Entry not found: %1").arg(name);
        return {};
    }
    if (found->isDirectory)
        return {};
    if (found->uncompressedSize > static_cast<quint64>(maxSize)) {
        m_error = QStringLiteral("Entry is larger than the read limit");
        return {};
    }

    // The central directory's name/extra lengths need not match the local
    // header's, so re-read the local header to find where the data really is.
    if (!m_file.seek(static_cast<qint64>(found->localHeaderOffset))) {
        m_error = QStringLiteral("Cannot seek to entry data");
        return {};
    }
    const QByteArray local = m_file.read(30);
    if (local.size() < 30 || le<quint32>(local, 0) != kSigLocalHeader) {
        m_error = QStringLiteral("Entry has a corrupt local header");
        return {};
    }
    const quint16 localNameLength  = le<quint16>(local, 26);
    const quint16 localExtraLength = le<quint16>(local, 28);

    const qint64 dataOffset = static_cast<qint64>(found->localHeaderOffset)
                              + 30 + localNameLength + localExtraLength;
    if (!m_file.seek(dataOffset)) {
        m_error = QStringLiteral("Entry data lies outside the archive");
        return {};
    }

    const QByteArray raw = m_file.read(static_cast<qint64>(found->compressedSize));
    if (static_cast<quint64>(raw.size()) != found->compressedSize) {
        m_error = QStringLiteral("Entry data is truncated");
        return {};
    }

    switch (found->compressionMethod) {
    case kMethodStore:
        return raw;
    case kMethodDeflate: {
        const QByteArray inflated = inflateRaw(raw, found->uncompressedSize, maxSize);
        if (inflated.isEmpty() && found->uncompressedSize > 0)
            m_error = QStringLiteral("Entry could not be decompressed");
        return inflated;
    }
    default:
        m_error = QStringLiteral("Unsupported compression method %1").arg(found->compressionMethod);
        return {};
    }
}

} // namespace vsp
