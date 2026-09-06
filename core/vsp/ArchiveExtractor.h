#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace vsp {

class ZipReader;

/// One file unpacked out of an archive, ready to be sent.
struct ExtractedFile
{
    QString localPath;    ///< where it was written
    QString relativePath; ///< its path within the theme folder, '/' separated
    qint64 size = 0;
};

struct ExtractionResult
{
    bool ok = false;
    QString error;
    QString rootPath;                  ///< the directory everything landed in
    QVector<ExtractedFile> files;
    qint64 totalBytes = 0;
    int skipped = 0;                   ///< entries refused as unsafe
};

/// Unpacks a subtree of an archive into a local directory.
///
/// This is the only code in the project that writes archive contents to disk,
/// so it is where zip-slip is stopped: every entry name is rebuilt component by
/// component from sanitised parts, and the resulting path is confirmed to be
/// inside the destination before the file is opened. An entry that cannot be
/// made safe is skipped and counted, never guessed at.
/// Ceilings that keep a hostile or simply enormous archive from filling the
/// disk. A theme is images and a text file; these are generous for that.
struct ExtractionLimits
{
    qint64 maxTotalBytes = 512LL * 1024 * 1024;
    qint64 maxFileBytes  = 64LL * 1024 * 1024;
    int maxFiles = 2000;
    int maxDepth = 8;
};

class ArchiveExtractor
{
public:
    /// Extracts every entry of \a archive under \a archivePrefix into
    /// \a destinationDir, preserving the structure below the prefix.
    static ExtractionResult extractSubtree(ZipReader *archive,
                                           const QString &archivePrefix,
                                           const QString &destinationDir,
                                           const ExtractionLimits &limits = ExtractionLimits());

    /// Enumerates an existing folder into the same shape, without copying
    /// anything: an unpacked game is already on disk, so the installer needs
    /// the file list, not a second copy of it. Names are still checked, since
    /// a local path can be as odd as an archive entry.
    static ExtractionResult collectDirectory(const QString &sourceDir,
                                             const ExtractionLimits &limits = ExtractionLimits());
};

} // namespace vsp
