#include "DropStageModel.h"

#include "MetadataDb.h"
#include "ThumbnailProvider.h"
#include "PathUtils.h"
#include "VitaPaths.h"

#include <QFile>
#include <QFileInfo>
#include <QUrl>

namespace vsp {
namespace {

// A NoNpDrm-style part is routed by which of app/addcont/license it came
// from, not by PackageKind -- one dropped folder can hold parts for more
// than one at once, which a single kind can't express.
QString destinationFor(const PackageInfo &info, const QString &mount)
{
    if (!info.noNpDrmRoot.isEmpty())
        return vitapaths::fixedRootFor(info.noNpDrmRoot, mount);
    return vitapaths::destinationDirFor(info.kind, mount);
}

QString kindLabelFor(const PackageInfo &info)
{
    if (info.noNpDrmRoot == QLatin1String("app"))
        return QStringLiteral("App Data");
    if (info.noNpDrmRoot == QLatin1String("addcont"))
        return QStringLiteral("Add-on Content");
    if (info.noNpDrmRoot == QLatin1String("license"))
        return QStringLiteral("License");
    return packageKindLabel(info.kind);
}

// Each package kind gets its own accent so the staging list reads at a glance
// without needing a written label for the category.
QString accentFor(PackageKind kind)
{
    switch (kind) {
    case PackageKind::Game:
    case PackageKind::FolderGame: return QStringLiteral("blue");
    case PackageKind::Homebrew: return QStringLiteral("green");
    case PackageKind::Patch:
    case PackageKind::AddOn:    return QStringLiteral("blue");
    case PackageKind::SystemTheme:
    case PackageKind::ShellTheme: return QStringLiteral("green");
    case PackageKind::Video:
    case PackageKind::Photo:
    case PackageKind::Music:    return QStringLiteral("green");
    case PackageKind::Unknown:  break;
    }
    return QStringLiteral("grey");
}

} // namespace

DropStageModel::DropStageModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void DropStageModel::setThumbnailProvider(ThumbnailProvider *provider)
{
    if (m_thumbnails_provider == provider)
        return;
    m_thumbnails_provider = provider;
    if (!provider)
        return;

    // A provider that needs to decode something answers late; match its answer
    // back to the row it belongs to.
    connect(provider, &ThumbnailProvider::thumbnailReady, this,
            [this](const QString &localPath, const QString &thumbnailUrl) {
                for (int row = 0; row < m_items.size(); ++row) {
                    if (m_items.at(row).localPath != localPath)
                        continue;
                    m_thumbnails[row] = thumbnailUrl;
                    emit dataChanged(index(row, 0), index(row, 0), { CoverSourceRole });
                    break;
                }
            });
}

void DropStageModel::setMount(const QString &mount)
{
    if (m_mount == mount)
        return;
    m_mount = mount;

    // Re-route everything still waiting: the user changed which card they mean.
    for (int row = 0; row < m_items.size(); ++row)
        m_destinations[row] = destinationFor(m_items.at(row), m_mount);

    if (!m_items.isEmpty()) {
        emit dataChanged(index(0, 0), index(static_cast<int>(m_items.size()) - 1, 0),
                         { DestinationRole });
    }
}

int DropStageModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QHash<int, QByteArray> DropStageModel::roleNames() const
{
    return {
        { TitleRole,       "title" },
        { TitleIdRole,     "titleId" },
        { SubtitleRole,    "subtitle" },
        { KindLabelRole,   "kindLabel" },
        { VersionRole,     "version" },
        { SizeTextRole,    "sizeText" },
        { CoverSourceRole, "coverSource" },
        { MatchedRole,     "matched" },
        { DestinationRole, "destination" },
        { LocalPathRole,   "localPath" },
        { ErrorRole,       "error" },
        { InstallableRole, "installable" },
        { AccentRole,      "accent" }
    };
}

QVariant DropStageModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};

    const PackageInfo &info = m_items.at(index.row());
    switch (role) {
    case TitleRole:     return info.displayName();
    case TitleIdRole:   return info.titleId;
    case SubtitleRole:
        // A folder is described by what it holds. A game keeps its Title ID
        // front and centre, exactly as a packaged one does; a theme has none,
        // so its author stands in.
        if (info.isDirectoryPayload()) {
            QStringList parts;
            if (!info.titleId.isEmpty())
                parts << info.titleId;
            if (!info.provider.isEmpty())
                parts << info.provider;
            if (info.entryCount > 0)
                parts << QStringLiteral("%1 files").arg(info.entryCount);
            // A game folder is usually named after its Title ID; saying so
            // twice tells the reader nothing the first mention did not.
            if (!parts.contains(info.fileName))
                parts << info.fileName;
            return parts.join(QStringLiteral("  ·  "));
        }
        if (!info.titleId.isEmpty() && !info.appVersion.isEmpty())
            return QStringLiteral("%1  ·  v%2").arg(info.titleId, info.appVersion);
        if (!info.titleId.isEmpty())
            return info.titleId;
        return info.fileName;
    case KindLabelRole: return kindLabelFor(info);
    case VersionRole:   return info.appVersion;
    case SizeTextRole:
        // For a theme the number that matters is what lands on the card.
        return path::humanSize(info.isDirectoryPayload() && info.unpackedSize > 0
                                   ? info.unpackedSize
                                   : info.fileSize);
    case CoverSourceRole: {
        // Cover art from the local database wins for a matched game; otherwise
        // whatever the file itself could be made to show.
        if (!info.coverPath.isEmpty())
            return QUrl::fromLocalFile(info.coverPath).toString();
        return m_thumbnails.value(index.row());
    }
    case MatchedRole:
        // A theme carries its own name, author and preview inside the archive,
        // so the title database has nothing to say about it. Marking it
        // "unmatched" would raise an alert about a state that is entirely
        // normal, which is exactly what the alert marker must not do.
        return info.isDirectoryPayload() || info.metadataMatched;
    case DestinationRole: return m_destinations.value(index.row());
    case LocalPathRole:   return info.localPath;
    case ErrorRole:       return info.error;
    case InstallableRole: return info.isInstallable();
    case AccentRole:      return accentFor(info.kind);
    default:              return {};
    }
}

int DropStageModel::readyCount() const
{
    int count = 0;
    for (const PackageInfo &info : m_items) {
        if (info.valid)
            ++count;
    }
    return count;
}

QString DropStageModel::totalSizeText() const
{
    qint64 total = 0;
    for (const PackageInfo &info : m_items)
        total += info.fileSize;
    return path::humanSize(total);
}

int DropStageModel::addPaths(const QStringList &localPaths)
{
    int added = 0;

    // One item, fully processed and inserted as its own row. A NoNpDrm-style
    // dump expands into several calls to this from one dropped path; anything
    // else is exactly one.
    auto appendInfo = [this, &added](PackageInfo info) {
        const QString absolute = info.localPath;
        const bool duplicate = std::any_of(m_items.cbegin(), m_items.cend(),
                                           [&absolute](const PackageInfo &existing) {
                                               return existing.localPath == absolute;
                                           });
        if (duplicate)
            return;

        if (m_db)
            m_db->resolve(&info);

        const QString thumbnail = m_thumbnails_provider
                                      ? m_thumbnails_provider->thumbnailFor(info)
                                      : QString();

        const int row = static_cast<int>(m_items.size());
        beginInsertRows({}, row, row);
        m_items.append(info);
        m_destinations.append(destinationFor(info, m_mount));
        m_thumbnails.append(thumbnail);
        endInsertRows();

        ++added;
        emit itemAdded(row, info.displayName());
    };

    for (const QString &raw : localPaths) {
        // Drops and file dialogs hand over URLs, not paths. A file:// URL
        // becomes a plain path; anything else (notably Android's content://
        // URIs, which the platform file engine opens directly) is kept whole.
        QString localPath = raw;
        if (localPath.startsWith(QLatin1String("file:")))
            localPath = QUrl(localPath).toLocalFile();

        const bool isOpaqueUri = localPath.contains(QLatin1String("://"));
        bool isDirectory = false;
        if (isOpaqueUri) {
            if (!QFile::exists(localPath))
                continue;
        } else {
            // Folders are accepted too: an already-unpacked game is dropped as
            // the folder named by its Title ID.
            const QFileInfo fileInfo(localPath);
            if (!fileInfo.exists())
                continue;
            if (!fileInfo.isFile() && !fileInfo.isDir())
                continue;
            isDirectory = fileInfo.isDir();
            localPath = fileInfo.absoluteFilePath();
        }

        const QString absolute = localPath;

        // A NoNpDrm-style dump is a parent folder to be split, not one
        // package -- one dropped path can become several staged rows, each
        // with its own fixed destination. See PackageInspector::inspectNoNpDrmDump().
        if (isDirectory && PackageInspector::looksLikeNoNpDrmDump(absolute)) {
            const QVector<PackageInfo> parts = PackageInspector::inspectNoNpDrmDump(absolute);
            for (const PackageInfo &part : parts)
                appendInfo(part);
            continue;
        }

        // The same shape, one level up: "app" or "addcont" (or "license")
        // dragged on its own rather than as part of the parent dump. Without
        // this check it either gets refused outright or -- if its inner
        // serial folder is dragged instead -- silently defaults to ux0:app
        // no matter which of the three it actually came from.
        if (isDirectory && PackageInspector::looksLikeStandaloneContentFolder(absolute)) {
            const QVector<PackageInfo> parts = PackageInspector::inspectStandaloneContentFolder(absolute);
            for (const PackageInfo &part : parts)
                appendInfo(part);
            continue;
        }

        PackageInfo info = PackageInspector::inspect(absolute);
        info.localPath = absolute;
        appendInfo(std::move(info));
    }

    if (added > 0)
        emit countChanged();
    return added;
}

void DropStageModel::removeAt(int row)
{
    if (row < 0 || row >= m_items.size())
        return;
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    m_destinations.removeAt(row);
    m_thumbnails.removeAt(row);
    endRemoveRows();
    emit countChanged();
}

void DropStageModel::clear()
{
    if (m_items.isEmpty())
        return;
    beginResetModel();
    m_items.clear();
    m_destinations.clear();
    m_thumbnails.clear();
    endResetModel();
    emit countChanged();
}

QVariantMap DropStageModel::itemAt(int row) const
{
    if (row < 0 || row >= m_items.size())
        return {};

    const PackageInfo &info = m_items.at(row);
    return {
        { QStringLiteral("title"),       info.displayName() },
        { QStringLiteral("titleId"),     info.titleId },
        { QStringLiteral("kindLabel"),   kindLabelFor(info) },
        { QStringLiteral("version"),     info.appVersion },
        { QStringLiteral("sizeText"),    path::humanSize(info.fileSize) },
        { QStringLiteral("localPath"),   info.localPath },
        { QStringLiteral("destination"), m_destinations.value(row) },
        { QStringLiteral("matched"),     info.isDirectoryPayload() || info.metadataMatched },
        { QStringLiteral("installable"), info.isInstallable() },
        { QStringLiteral("error"),       info.error }
    };
}

void DropStageModel::setDestination(int row, const QString &remoteDir)
{
    if (row < 0 || row >= m_destinations.size())
        return;
    const QString normalized = path::normalizeRemote(remoteDir);
    if (m_destinations.at(row) == normalized)
        return;
    m_destinations[row] = normalized;
    emit dataChanged(index(row, 0), index(row, 0), { DestinationRole });
}

} // namespace vsp
