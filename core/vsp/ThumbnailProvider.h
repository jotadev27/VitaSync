#pragma once

#include <QObject>
#include <QString>

namespace vsp {

struct PackageInfo;

/// Turns a staged file into a small preview image.
///
/// The interface lives in the core so the models can ask for a thumbnail, but
/// the implementation does not: rendering needs Qt Gui (and, for video frames,
/// a decoder), and the core is deliberately free of both. The application
/// injects a provider; without one, cards fall back to their generated plate.
///
/// A provider may answer immediately or later. Returning an empty string is
/// not a refusal -- it means "not yet", and `thumbnailReady` will follow if the
/// work succeeds.
class ThumbnailProvider : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailProvider(QObject *parent = nullptr) : QObject(parent) { }

    /// Requests a thumbnail for \a info. Returns a file URL, or an empty
    /// string when the answer will arrive through `thumbnailReady`.
    virtual QString thumbnailFor(const PackageInfo &info) = 0;

signals:
    /// \a localPath identifies the staged file; \a thumbnailUrl is a file URL.
    void thumbnailReady(const QString &localPath, const QString &thumbnailUrl);
};

} // namespace vsp
