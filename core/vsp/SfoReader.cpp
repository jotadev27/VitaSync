#include "SfoReader.h"

#include <QtEndian>

#include <array>
#include <cstring>

namespace vsp {
namespace {

constexpr quint32 kSfoMagic       = 0x46535000u;   // "\0PSF" little-endian
constexpr int     kHeaderSize     = 20;
constexpr int     kIndexEntrySize = 16;

// Sony's data format tags.
constexpr quint16 kFmtUtf8Special = 0x0004;        // not NUL-terminated
constexpr quint16 kFmtUtf8        = 0x0204;
constexpr quint16 kFmtInt32       = 0x0404;

// Every read from the untrusted blob funnels through this, and the bounds
// check above the copy is what makes the rest of the parser safe on hostile
// input (see the truncated-file case in tst_core).
//
// GCC cannot relate QByteArray::size() to the extent of the buffer
// constData() returns -- it assumes the shared empty array, a char[1] -- and
// warns about a read this function has already made impossible. The warning is
// silenced for these few lines only.
QT_WARNING_PUSH
QT_WARNING_DISABLE_GCC("-Warray-bounds")
template <typename T>
bool readLE(const QByteArray &blob, int offset, T *out)
{
    if (offset < 0 || offset > blob.size() - static_cast<int>(sizeof(T)))
        return false;
    std::array<uchar, sizeof(T)> buffer {};
    std::memcpy(buffer.data(), blob.constData() + offset, sizeof(T));
    *out = qFromLittleEndian<T>(buffer.data());
    return true;
}
QT_WARNING_POP

QString readKey(const QByteArray &blob, int offset)
{
    if (offset < 0 || offset >= blob.size())
        return {};
    const int end = blob.indexOf('\0', offset);
    const int length = (end < 0 ? blob.size() : end) - offset;
    return QString::fromUtf8(blob.constData() + offset, length);
}

} // namespace

bool SfoReader::parse(const QByteArray &blob)
{
    m_valid = false;
    m_error.clear();
    m_values.clear();

    if (blob.size() < kHeaderSize) {
        m_error = QStringLiteral("param.sfo is truncated");
        return false;
    }

    quint32 magic = 0, keyTableStart = 0, dataTableStart = 0, tableEntries = 0;
    readLE(blob, 0, &magic);
    readLE(blob, 8, &keyTableStart);
    readLE(blob, 12, &dataTableStart);
    readLE(blob, 16, &tableEntries);

    if (magic != kSfoMagic) {
        m_error = QStringLiteral("param.sfo has an unexpected signature");
        return false;
    }
    // A sane package has a few dozen keys; anything wilder is malformed input.
    if (tableEntries > 1024) {
        m_error = QStringLiteral("param.sfo declares an implausible entry count");
        return false;
    }
    if (keyTableStart > static_cast<quint32>(blob.size())
        || dataTableStart > static_cast<quint32>(blob.size())) {
        m_error = QStringLiteral("param.sfo table offsets fall outside the file");
        return false;
    }

    for (quint32 i = 0; i < tableEntries; ++i) {
        const int entryOffset = kHeaderSize + static_cast<int>(i) * kIndexEntrySize;

        quint16 keyOffset = 0, format = 0;
        quint32 dataLength = 0, dataMaxLength = 0, dataOffset = 0;
        if (!readLE(blob, entryOffset, &keyOffset)
            || !readLE(blob, entryOffset + 2, &format)
            || !readLE(blob, entryOffset + 4, &dataLength)
            || !readLE(blob, entryOffset + 8, &dataMaxLength)
            || !readLE(blob, entryOffset + 12, &dataOffset)) {
            m_error = QStringLiteral("param.sfo index table is truncated");
            return false;
        }
        Q_UNUSED(dataMaxLength)

        const QString key = readKey(blob, static_cast<int>(keyTableStart) + keyOffset);
        if (key.isEmpty())
            continue;

        const qint64 absolute = static_cast<qint64>(dataTableStart) + dataOffset;
        if (absolute < 0 || absolute + static_cast<qint64>(dataLength) > blob.size())
            continue;                            // skip the entry, don't fail the file

        switch (format) {
        case kFmtInt32: {
            quint32 value = 0;
            if (dataLength >= 4 && readLE(blob, static_cast<int>(absolute), &value))
                m_values.insert(key, static_cast<int>(value));
            break;
        }
        case kFmtUtf8:
        case kFmtUtf8Special:
        default: {
            QByteArray raw(blob.constData() + absolute, static_cast<int>(dataLength));
            const int nul = raw.indexOf('\0');
            if (nul >= 0)
                raw.truncate(nul);
            m_values.insert(key, QString::fromUtf8(raw).trimmed());
            break;
        }
        }
    }

    m_valid = !m_values.isEmpty();
    if (!m_valid)
        m_error = QStringLiteral("param.sfo contained no readable keys");
    return m_valid;
}

QString SfoReader::string(const QString &key) const
{
    return m_values.value(key).toString();
}

int SfoReader::integer(const QString &key, int fallback) const
{
    const QVariant value = m_values.value(key);
    bool ok = false;
    const int parsed = value.toInt(&ok);
    return ok ? parsed : fallback;
}

QString SfoReader::titleId() const
{
    const QString id = string(QStringLiteral("TITLE_ID"));
    return id.isEmpty() ? string(QStringLiteral("TITLEID000")) : id;
}

QString SfoReader::title() const
{
    // STITLE is the short home-screen label; TITLE is the full one.
    const QString full = string(QStringLiteral("TITLE"));
    return full.isEmpty() ? string(QStringLiteral("STITLE")) : full;
}

QString SfoReader::appVersion() const
{
    const QString version = string(QStringLiteral("APP_VER"));
    return version.isEmpty() ? string(QStringLiteral("VERSION")) : version;
}

QString SfoReader::category() const
{
    return string(QStringLiteral("CATEGORY"));
}

} // namespace vsp
