#pragma once

#include "RemoteTransport.h"

#include <QAbstractListModel>
#include <QSet>
#include <QVector>

namespace vsp {

/// The remote directory listing, with selection state built in.
///
/// Selection lives in the model rather than the view because drag-select,
/// ctrl-click and the touch lasso all have to agree on one set, and because
/// every batch action (download, delete, move) reads that same set.
class RemoteBrowserModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(qint64 selectionBytes READ selectionBytes NOTIFY selectionChanged)
    Q_PROPERTY(QString selectionSummary READ selectionSummary NOTIFY selectionChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PathRole,
        IsDirectoryRole,
        SizeRole,
        SizeTextRole,
        ModifiedTextRole,
        SelectedRole,
        IconNameRole,
        KindLabelRole,
        ProtectedRole
    };

    explicit RemoteBrowserModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(const QVector<RemoteEntry> &entries);
    const QVector<RemoteEntry> &entries() const { return m_entries; }

    int selectionCount() const { return static_cast<int>(m_selected.size()); }
    qint64 selectionBytes() const;
    QString selectionSummary() const;

    Q_INVOKABLE void setSelected(int row, bool selected);
    Q_INVOKABLE void toggleSelected(int row);
    Q_INVOKABLE void selectRange(int fromRow, int toRow, bool selected);
    Q_INVOKABLE void selectOnly(int row);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(int row) const;

    Q_INVOKABLE QStringList selectedPaths() const;
    Q_INVOKABLE QStringList selectedNames() const;
    Q_INVOKABLE QVariantList selectedItems() const;
    Q_INVOKABLE QVariantMap itemAt(int row) const;

signals:
    void selectionChanged();
    void countChanged();

private:
    QVector<RemoteEntry> m_entries;
    QSet<int> m_selected;
};

} // namespace vsp
