#pragma once

#include "PackageInspector.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace vsp {

/// Offline title/cover-art database.
///
/// Nothing in here touches the network, by design: the index is a JSON file
/// shipped with the app, and cover images are read from local folders. Users
/// with a Vita cover pack point the app at that folder and get real art; users
/// without one still get the exact game name from the package's own param.sfo
/// and a generated placeholder, and the install is never blocked either way.
class MetadataDb : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int titleCount READ titleCount NOTIFY changed)
    Q_PROPERTY(QString coverPackPath READ coverPackPath WRITE setCoverPackPath NOTIFY changed)
    Q_PROPERTY(int coverCount READ coverCount NOTIFY changed)

public:
    struct TitleRecord
    {
        QString titleId;
        QString name;
        QString region;
        QString publisher;
    };

    explicit MetadataDb(QObject *parent = nullptr);

    /// Loads the bundled index. \a indexPath may be a qrc path.
    bool load(const QString &indexPath);

    int titleCount() const { return static_cast<int>(m_titles.size()); }
    int coverCount() const { return static_cast<int>(m_covers.size()); }

    /// Folder of `TITLEID.png` images, as distributed in Vita cover packs.
    QString coverPackPath() const { return m_coverPackPath; }
    void setCoverPackPath(const QString &directory);

    Q_INVOKABLE bool hasTitle(const QString &titleId) const;
    Q_INVOKABLE QString nameFor(const QString &titleId) const;
    Q_INVOKABLE QString coverUrlFor(const QString &titleId) const;

    /// Fills the metadata fields of \a info in place. Leaves the param.sfo
    /// values alone when they are present -- the package is the authority on
    /// its own name; the database only fills gaps and adds art.
    void resolve(PackageInfo *info) const;

signals:
    void changed();

private:
    void indexCoverDirectory(const QString &directory);

    QHash<QString, TitleRecord> m_titles;
    QHash<QString, QString> m_covers;     ///< title id -> absolute image path
    QString m_coverPackPath;
    QStringList m_coverRoots;
};

} // namespace vsp
