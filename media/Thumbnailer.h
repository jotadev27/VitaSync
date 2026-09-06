#pragma once

#include <vsp/ThumbnailProvider.h>

#include <QHash>
#include <QQueue>
#include <QString>

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

namespace vsp {

/// Draws the small preview shown next to each staged item.
///
/// Everything is local and derived from the file already on disk: an embedded
/// icon if the package carries one, the image itself for a photo, a decoded
/// frame for a video, and a palette drawn from the theme's own declared colours
/// for a theme that ships no artwork. Nothing is fetched.
///
/// Photos and embedded icons are cheap and answered on the spot. Video needs a
/// decoder, so it is done one file at a time in the background and reported
/// through `thumbnailReady`.
class Thumbnailer : public ThumbnailProvider
{
    Q_OBJECT

public:
    /// \a cacheDir holds the rendered thumbnails; it is created on demand.
    explicit Thumbnailer(const QString &cacheDir, QObject *parent = nullptr);
    ~Thumbnailer() override;

    /// True when this build can decode a video frame. When false, videos fall
    /// back to a drawn plate rather than showing nothing.
    static bool canReadVideoFrames();

    QString thumbnailFor(const PackageInfo &info) override;

    /// Edge length of the rendered square, in pixels.
    static constexpr int kSize = 160;

private:
    QString cachePathFor(const PackageInfo &info) const;
    bool saveSquare(const QImage &source, const QString &target) const;

    bool renderEmbedded(const PackageInfo &info, const QString &target) const;
    bool renderPhoto(const PackageInfo &info, const QString &target) const;
    bool renderThemePlate(const PackageInfo &info, const QString &target) const;
    bool renderMediaPlate(const PackageInfo &info, const QString &target) const;

    void enqueueVideo(const QString &localPath, const QString &target);
    void startNextVideo();
    void finishVideo(const QString &localPath, const QString &target, bool ok);

    struct VideoJob
    {
        QString localPath;
        QString target;
    };

    QString m_cacheDir;
    QQueue<VideoJob> m_videoQueue;
    bool m_videoBusy = false;
    class VideoGrabber *m_grabber = nullptr;
};

} // namespace vsp
