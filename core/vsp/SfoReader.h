#pragma once

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVariantMap>

namespace vsp {

/// Reader for the PSF/SFO key-value blob Sony ships inside every package as
/// `sce_sys/param.sfo`. This is where the authoritative Title ID and the exact
/// on-screen game name come from -- we never guess either from the file name.
class SfoReader
{
public:
    /// Parses an in-memory param.sfo. Returns false and leaves the reader empty
    /// on any structural problem; the blob comes from an untrusted archive so
    /// every offset is bounds-checked before use.
    bool parse(const QByteArray &blob);

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }

    QVariantMap values() const { return m_values; }
    bool contains(const QString &key) const { return m_values.contains(key); }
    QString string(const QString &key) const;
    int integer(const QString &key, int fallback = 0) const;

    // Convenience accessors for the fields the UI actually shows.
    QString titleId() const;          ///< e.g. "PCSE00001"
    QString title() const;            ///< e.g. "Uncharted: Golden Abyss"
    QString appVersion() const;       ///< e.g. "01.00"
    QString category() const;         ///< "gd" = game, "gp" = patch, "ac" = add-on

private:
    bool m_valid = false;
    QString m_error;
    QVariantMap m_values;
};

} // namespace vsp
