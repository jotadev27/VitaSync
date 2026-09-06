#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>
#include <QStringList>

namespace vsp {

/// Minimal read-only ZIP reader, enough to pull individual members out of a VPK
/// or theme package without unpacking the whole (potentially multi-gigabyte)
/// archive. Supports store + deflate and ZIP64 offsets/sizes.
///
/// Deliberately not a general-purpose extractor: it only ever returns member
/// bytes in memory, so there is no path-traversal surface at all.
class ZipReader
{
public:
    struct Entry
    {
        QString name;
        quint16 compressionMethod = 0;
        quint64 compressedSize = 0;
        quint64 uncompressedSize = 0;
        quint64 localHeaderOffset = 0;
        quint32 crc32 = 0;
        bool isDirectory = false;
    };

    ZipReader() = default;
    explicit ZipReader(const QString &filePath);
    ~ZipReader();

    ZipReader(const ZipReader &) = delete;
    ZipReader &operator=(const ZipReader &) = delete;

    bool open(const QString &filePath);
    void close();

    bool isOpen() const { return m_file.isOpen() && m_indexed; }
    QString errorString() const { return m_error; }

    QStringList entryNames() const;
    bool contains(const QString &name) const;
    const Entry *entry(const QString &name) const;

    /// Case-insensitive lookup, because packagers are inconsistent about
    /// `sce_sys` vs `SCE_SYS`.
    const Entry *findEntryInsensitive(const QString &name) const;

    /// Reads and decompresses one member. \a maxSize caps the allocation so a
    /// hostile archive cannot make us commit gigabytes for a "param.sfo".
    QByteArray read(const QString &name, qint64 maxSize = 16 * 1024 * 1024);

private:
    bool buildIndex();
    qint64 findEndOfCentralDirectory();
    bool readCentralDirectory(qint64 offset, quint64 entryCount);
    static void applyZip64Extra(Entry *entry, const QByteArray &extra,
                                bool needUncompressed, bool needCompressed,
                                bool needOffset);

    QFile m_file;
    QHash<QString, Entry> m_entries;
    QStringList m_order;
    QString m_error;
    bool m_indexed = false;
};

} // namespace vsp
