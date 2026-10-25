#pragma once

#include <QQuickImageProvider>
#include <QHash>
#include <QMutex>

// Raster file-type artwork supplied by the desktop, without reading a user's
// files or redistributing operating-system artwork with the application.
class FileTypeImages final : public QQuickImageProvider
{
public:
    FileTypeImages();
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
private:
    QHash<QString, QImage> m_images;
    QMutex m_mutex;
};
