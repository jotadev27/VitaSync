#pragma once

#include "PackageInspector.h"

#include <QAbstractListModel>
#include <QList>

namespace vsp {

class MetadataDb;
class ThumbnailProvider;

/// Files the user has dropped but not yet sent.
///
/// The drop is identified immediately -- exact name and Title ID out of
/// param.sfo, cover art out of the local database -- and then waits for Start.
/// Nothing is uploaded until the user has seen what the app thinks it is.
class DropStageModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY countChanged)
    Q_PROPERTY(QString totalSizeText READ totalSizeText NOTIFY countChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        TitleIdRole,
        SubtitleRole,
        KindLabelRole,
        VersionRole,
        SizeTextRole,
        CoverSourceRole,
        MatchedRole,
        DestinationRole,
        LocalPathRole,
        ErrorRole,
        InstallableRole,
        AccentRole
    };

    explicit DropStageModel(QObject *parent = nullptr);

    void setMetadataDb(MetadataDb *db) { m_db = db; }

    /// Optional. Without one, cards use their generated plate.
    void setThumbnailProvider(ThumbnailProvider *provider);
    void setMount(const QString &mount);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int readyCount() const;
    QString totalSizeText() const;

    /// Inspects \a localPaths and adds whatever is usable. Returns how many
    /// were added. Duplicates are ignored rather than queued twice.
    int addPaths(const QStringList &localPaths);

    const QList<PackageInfo> &items() const { return m_items; }

    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QVariantMap itemAt(int row) const;
    Q_INVOKABLE void setDestination(int row, const QString &remoteDir);

    /// The folder each item will be written to, in row order.
    QStringList destinations() const { return m_destinations; }

signals:
    void countChanged();
    void itemAdded(int row, const QString &title);

private:
    QList<PackageInfo> m_items;
    QStringList m_destinations;
    QStringList m_thumbnails;      ///< one per item, parallel to m_items
    ThumbnailProvider *m_thumbnails_provider = nullptr;
    MetadataDb *m_db = nullptr;
    QString m_mount = QStringLiteral("ux0:");
};

} // namespace vsp
