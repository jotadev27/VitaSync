#include "RemoteBrowserModel.h"

#include "PackageInspector.h"
#include "PathUtils.h"
#include "VitaPaths.h"

#include <QLocale>

namespace vsp {
namespace {

QString iconNameFor(const RemoteEntry &entry)
{
    if (entry.isDirectory)
        return QStringLiteral("folder");

    switch (PackageInspector::kindForExtension(entry.name)) {
    case PackageKind::Video: return QStringLiteral("video");
    case PackageKind::Photo: return QStringLiteral("photo");
    case PackageKind::Music: return QStringLiteral("music");
    default: break;
    }
    if (entry.name.endsWith(QLatin1String(".vpk"), Qt::CaseInsensitive))
        return QStringLiteral("package");
    if (entry.name.endsWith(QLatin1String(".bin"), Qt::CaseInsensitive)
        || entry.name.endsWith(QLatin1String(".self"), Qt::CaseInsensitive)
        || entry.name.endsWith(QLatin1String(".suprx"), Qt::CaseInsensitive)
        || entry.name.endsWith(QLatin1String(".skprx"), Qt::CaseInsensitive)) {
        return QStringLiteral("binary");
    }
    return QStringLiteral("file");
}

} // namespace

RemoteBrowserModel::RemoteBrowserModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int RemoteBrowserModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QHash<int, QByteArray> RemoteBrowserModel::roleNames() const
{
    return {
        { NameRole,         "name" },
        { PathRole,         "path" },
        { IsDirectoryRole,  "isDirectory" },
        { SizeRole,         "size" },
        { SizeTextRole,     "sizeText" },
        { ModifiedTextRole, "modifiedText" },
        { SelectedRole,     "selected" },
        { IconNameRole,     "iconName" },
        { KindLabelRole,    "kindLabel" },
        { ProtectedRole,    "isProtected" }
    };
}

QVariant RemoteBrowserModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const RemoteEntry &entry = m_entries.at(index.row());
    switch (role) {
    case NameRole:        return entry.name;
    case PathRole:        return entry.path;
    case IsDirectoryRole: return entry.isDirectory;
    case SizeRole:        return entry.size;
    case SizeTextRole:    return entry.isDirectory ? QString() : path::humanSize(entry.size);
    case ModifiedTextRole:
        return entry.modified.isValid()
                   ? QLocale().toString(entry.modified, QStringLiteral("dd MMM yyyy  HH:mm"))
                   : QString();
    case SelectedRole:    return m_selected.contains(index.row());
    case IconNameRole:    return iconNameFor(entry);
    case KindLabelRole:
        return entry.isDirectory
                   ? QStringLiteral("Folder")
                   : packageKindLabel(PackageInspector::kindForExtension(entry.name));
    case ProtectedRole:   return vitapaths::isProtectedPath(entry.path);
    default:              return {};
    }
}

void RemoteBrowserModel::setEntries(const QVector<RemoteEntry> &entries)
{
    beginResetModel();
    m_entries = entries;
    m_selected.clear();
    endResetModel();
    emit selectionChanged();
    emit countChanged();
}

qint64 RemoteBrowserModel::selectionBytes() const
{
    qint64 total = 0;
    for (const int row : m_selected) {
        if (row >= 0 && row < m_entries.size() && !m_entries.at(row).isDirectory)
            total += qMax<qint64>(m_entries.at(row).size, 0);
    }
    return total;
}

QString RemoteBrowserModel::selectionSummary() const
{
    const int count = selectionCount();
    if (count == 0)
        return {};
    const qint64 bytes = selectionBytes();
    if (bytes <= 0)
        return QStringLiteral("%1 selected").arg(count);
    return QStringLiteral("%1 · %2").arg(count).arg(path::humanSize(bytes));
}

void RemoteBrowserModel::setSelected(int row, bool selected)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const bool had = m_selected.contains(row);
    if (had == selected)
        return;

    if (selected)
        m_selected.insert(row);
    else
        m_selected.remove(row);

    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, { SelectedRole });
    emit selectionChanged();
}

void RemoteBrowserModel::toggleSelected(int row)
{
    setSelected(row, !isSelected(row));
}

void RemoteBrowserModel::selectRange(int fromRow, int toRow, bool selected)
{
    if (m_entries.isEmpty())
        return;

    int first = qBound(0, qMin(fromRow, toRow), static_cast<int>(m_entries.size()) - 1);
    int last  = qBound(0, qMax(fromRow, toRow), static_cast<int>(m_entries.size()) - 1);

    bool touched = false;
    for (int row = first; row <= last; ++row) {
        const bool had = m_selected.contains(row);
        if (had == selected)
            continue;
        if (selected)
            m_selected.insert(row);
        else
            m_selected.remove(row);
        touched = true;
    }

    if (!touched)
        return;
    emit dataChanged(index(first, 0), index(last, 0), { SelectedRole });
    emit selectionChanged();
}

void RemoteBrowserModel::selectOnly(int row)
{
    clearSelection();
    setSelected(row, true);
}

void RemoteBrowserModel::selectAll()
{
    if (m_entries.isEmpty())
        return;
    selectRange(0, static_cast<int>(m_entries.size()) - 1, true);
}

void RemoteBrowserModel::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    emit dataChanged(index(0, 0), index(static_cast<int>(m_entries.size()) - 1, 0),
                     { SelectedRole });
    emit selectionChanged();
}

bool RemoteBrowserModel::isSelected(int row) const
{
    return m_selected.contains(row);
}

QStringList RemoteBrowserModel::selectedPaths() const
{
    QList<int> rows(m_selected.cbegin(), m_selected.cend());
    std::sort(rows.begin(), rows.end());

    QStringList paths;
    paths.reserve(rows.size());
    for (const int row : std::as_const(rows)) {
        if (row >= 0 && row < m_entries.size())
            paths.append(m_entries.at(row).path);
    }
    return paths;
}

QStringList RemoteBrowserModel::selectedNames() const
{
    QList<int> rows(m_selected.cbegin(), m_selected.cend());
    std::sort(rows.begin(), rows.end());

    QStringList names;
    names.reserve(rows.size());
    for (const int row : std::as_const(rows)) {
        if (row >= 0 && row < m_entries.size())
            names.append(m_entries.at(row).name);
    }
    return names;
}

QVariantMap RemoteBrowserModel::itemAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    const RemoteEntry &entry = m_entries.at(row);
    return {
        { QStringLiteral("name"), entry.name },
        { QStringLiteral("path"), entry.path },
        { QStringLiteral("isDirectory"), entry.isDirectory },
        { QStringLiteral("size"), entry.size }
    };
}

QVariantList RemoteBrowserModel::selectedItems() const
{
    QList<int> rows(m_selected.cbegin(), m_selected.cend());
    std::sort(rows.begin(), rows.end());

    QVariantList items;
    items.reserve(rows.size());
    for (const int row : std::as_const(rows))
        items.append(itemAt(row));
    return items;
}

} // namespace vsp
