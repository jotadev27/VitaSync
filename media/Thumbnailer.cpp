#include "Thumbnailer.h"

#include <vsp/PackageInspector.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QUrl>

#ifdef VSP_HAVE_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QTimer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

namespace vsp {
namespace {

// The app's own palette, so a drawn plate sits in the interface rather than on
// top of it. Kept in step with desktop/qml/Theme.qml by hand -- there are four
// values and they have not moved.
const QColor kBase(QStringLiteral("#0C121C"));
const QColor kSurface(QStringLiteral("#111A28"));
const QColor kBlue(QStringLiteral("#2BB8F0"));
const QColor kGreen(QStringLiteral("#3FD79B"));

/// A stable number for a string, so the same file always draws the same plate.
uint stableHash(const QString &text)
{
    const QByteArray digest =
        QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5);
    return (static_cast<uchar>(digest.at(0)) << 16)
           | (static_cast<uchar>(digest.at(1)) << 8)
           | static_cast<uchar>(digest.at(2));
}

} // namespace

#ifdef VSP_HAVE_MULTIMEDIA
/// Pulls a single frame out of a video file and then gets out of the way.
///
/// The player is driven muted and is stopped the moment a usable frame lands.
/// A frame early in the file is often black, so it seeks a little way in before
/// grabbing, and gives up rather than hanging if the file will not decode.
class VideoGrabber : public QObject
{
    Q_OBJECT

public:
    explicit VideoGrabber(QObject *parent = nullptr)
        : QObject(parent)
        , m_player(new QMediaPlayer(this))
        , m_sink(new QVideoSink(this))
        , m_audio(new QAudioOutput(this))
        , m_deadline(new QTimer(this))
    {
        m_audio->setMuted(true);
        m_player->setAudioOutput(m_audio);
        m_player->setVideoSink(m_sink);

        m_deadline->setSingleShot(true);
        m_deadline->setInterval(6000);
        connect(m_deadline, &QTimer::timeout, this, [this] { finish(QImage()); });

        connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
                [this](QMediaPlayer::MediaStatus status) {
                    if (status == QMediaPlayer::InvalidMedia) {
                        finish(QImage());
                        return;
                    }
                    if (status == QMediaPlayer::LoadedMedia && !m_seeked) {
                        m_seeked = true;
                        // A tenth of the way in, capped, avoids the black frame
                        // most files open on.
                        const qint64 duration = m_player->duration();
                        m_player->setPosition(duration > 0
                                                  ? qMin<qint64>(duration / 10, 3000)
                                                  : 0);
                        m_player->play();
                    }
                });

        connect(m_player, &QMediaPlayer::errorOccurred, this,
                [this](QMediaPlayer::Error, const QString &) { finish(QImage()); });

        connect(m_sink, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame &frame) {
                    if (m_done || !frame.isValid())
                        return;
                    const QImage image = frame.toImage();
                    if (image.isNull() || image.width() < 2)
                        return;
                    finish(image);
                });
    }

    void grab(const QString &localPath)
    {
        m_done = false;
        m_seeked = false;
        m_player->setSource(QUrl::fromLocalFile(localPath));
        m_deadline->start();
    }

signals:
    void grabbed(const QImage &frame);

private:
    void finish(const QImage &image)
    {
        if (m_done)
            return;
        m_done = true;
        m_deadline->stop();
        m_player->stop();
        m_player->setSource(QUrl());
        emit grabbed(image);
    }

    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QAudioOutput *m_audio = nullptr;
    QTimer *m_deadline = nullptr;
    bool m_done = false;
    bool m_seeked = false;
};
#else
class VideoGrabber : public QObject
{
    Q_OBJECT
public:
    explicit VideoGrabber(QObject *parent = nullptr) : QObject(parent) { }
    void grab(const QString &) { emit grabbed(QImage()); }
signals:
    void grabbed(const QImage &frame);
};
#endif

// --- Thumbnailer ----------------------------------------------------------

Thumbnailer::Thumbnailer(const QString &cacheDir, QObject *parent)
    : ThumbnailProvider(parent)
    , m_cacheDir(cacheDir)
{
    QDir().mkpath(m_cacheDir);

    m_grabber = new VideoGrabber(this);
    connect(m_grabber, &VideoGrabber::grabbed, this, [this](const QImage &frame) {
        if (m_videoQueue.isEmpty()) {
            m_videoBusy = false;
            return;
        }
        const VideoJob job = m_videoQueue.dequeue();
        const bool ok = !frame.isNull() && saveSquare(frame, job.target);
        finishVideo(job.localPath, job.target, ok);
    });
}

Thumbnailer::~Thumbnailer() = default;

bool Thumbnailer::canReadVideoFrames()
{
#ifdef VSP_HAVE_MULTIMEDIA
    return true;
#else
    return false;
#endif
}

QString Thumbnailer::cachePathFor(const PackageInfo &info) const
{
    // Keyed on identity plus size, so replacing a file replaces its preview.
    const QFileInfo fileInfo(info.localPath);
    const QString key = QStringLiteral("%1|%2|%3")
                            .arg(info.localPath)
                            .arg(info.fileSize)
                            .arg(fileInfo.lastModified().toMSecsSinceEpoch());
    const QString digest =
        QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(),
                                                     QCryptographicHash::Sha1)
                                .toHex()
                                .left(20));
    return QDir(m_cacheDir).filePath(digest + QStringLiteral(".png"));
}

bool Thumbnailer::saveSquare(const QImage &source, const QString &target) const
{
    if (source.isNull())
        return false;

    // Cover, then centre-crop: a preview that keeps the subject beats one that
    // letterboxes it into a strip.
    const QImage scaled = source.scaled(kSize, kSize, Qt::KeepAspectRatioByExpanding,
                                        Qt::SmoothTransformation);
    const int x = qMax(0, (scaled.width() - kSize) / 2);
    const int y = qMax(0, (scaled.height() - kSize) / 2);
    const QImage cropped = scaled.copy(x, y, kSize, kSize);

    return cropped.save(target, "PNG");
}

QString Thumbnailer::thumbnailFor(const PackageInfo &info)
{
    if (info.localPath.isEmpty())
        return {};

    const QString target = cachePathFor(info);
    if (QFileInfo::exists(target))
        return QUrl::fromLocalFile(target).toString();

    // A package that carries its own artwork always wins: a game's icon0.png,
    // a theme's declared preview.
    if (!info.iconPng.isEmpty() && renderEmbedded(info, target))
        return QUrl::fromLocalFile(target).toString();

    switch (info.kind) {
    case PackageKind::Photo:
        if (renderPhoto(info, target))
            return QUrl::fromLocalFile(target).toString();
        break;

    case PackageKind::Video:
        if (canReadVideoFrames()) {
            // Answered later, once a frame has been decoded.
            enqueueVideo(info.localPath, target);
            return {};
        }
        if (renderMediaPlate(info, target))
            return QUrl::fromLocalFile(target).toString();
        break;

    case PackageKind::SystemTheme:
    case PackageKind::ShellTheme:
        if (renderThemePlate(info, target))
            return QUrl::fromLocalFile(target).toString();
        break;

    case PackageKind::Music:
        if (renderMediaPlate(info, target))
            return QUrl::fromLocalFile(target).toString();
        break;

    default:
        break;
    }

    return {};
}

bool Thumbnailer::renderEmbedded(const PackageInfo &info, const QString &target) const
{
    QImage image;
    if (!image.loadFromData(info.iconPng))
        return false;
    return saveSquare(image, target);
}

bool Thumbnailer::renderPhoto(const PackageInfo &info, const QString &target) const
{
    QImageReader reader(info.localPath);
    reader.setAutoTransform(true);          // honour the EXIF orientation

    const QSize size = reader.size();
    if (size.isValid() && size.width() > 0 && size.height() > 0) {
        // Ask the decoder for something near the size we need rather than
        // decoding a 48-megapixel photo in full to throw most of it away.
        const qreal factor = qMax(qreal(kSize) / size.width(),
                                  qreal(kSize) / size.height());
        if (factor < 1.0) {
            reader.setScaledSize(QSize(qMax(1, qRound(size.width() * factor)),
                                       qMax(1, qRound(size.height() * factor))));
        }
    }

    const QImage image = reader.read();
    if (image.isNull())
        return false;
    return saveSquare(image, target);
}

bool Thumbnailer::renderThemePlate(const PackageInfo &info, const QString &target) const
{
    // A theme with no artwork of its own still declares colours; show those
    // rather than two letters that say nothing about it.
    QList<QColor> palette;
    for (const QString &value : info.accentColors) {
        QColor colour(value);
        if (!colour.isValid())
            continue;
        // Themes routinely declare near-black backgrounds. Taken literally
        // that draws an unreadable square, so anything this dark is lifted
        // until it reads as a colour while keeping its hue.
        if (colour.lightness() < 70)
            colour = QColor::fromHsl(colour.hue() < 0 ? 210 : colour.hue(),
                                     qMax(60, colour.hslSaturation()), 110);
        palette.append(colour);
    }
    if (palette.isEmpty()) {
        const uint hash = stableHash(info.displayName());
        palette.append(QColor::fromHsv(static_cast<int>(hash % 360), 170, 220));
        palette.append(QColor::fromHsv(static_cast<int>((hash / 7) % 360), 150, 160));
    }
    while (palette.size() < 3)
        palette.append(palette.at(palette.size() % palette.size()).darker(140));

    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(kBase);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // A home screen in miniature: a wallpaper band, a status bar, and a row of
    // bubbles picking up the theme's own colours.
    const QRectF screen(14, 22, kSize - 28, kSize - 44);
    QLinearGradient wallpaper(screen.topLeft(), screen.bottomRight());
    wallpaper.setColorAt(0.0, palette.at(0).darker(115));
    wallpaper.setColorAt(1.0, palette.at(qMin(1, palette.size() - 1)).darker(160));

    QPainterPath frame;
    frame.addRoundedRect(screen, 6, 6);
    painter.fillPath(frame, wallpaper);

    painter.fillRect(QRectF(screen.left(), screen.top(), screen.width(), 9),
                     palette.at(qMin(2, palette.size() - 1)).darker(180));

    const qreal bubble = screen.width() / 5.2;
    for (int i = 0; i < 3; ++i) {
        const QRectF dot(screen.left() + 9 + i * (bubble + 7),
                         screen.top() + 26, bubble, bubble);
        QPainterPath rounded;
        rounded.addRoundedRect(dot, bubble / 3.0, bubble / 3.0);
        painter.fillPath(rounded, palette.at(i % palette.size()));
    }

    painter.setPen(QPen(palette.at(0), 1.5));
    painter.drawPath(frame);
    painter.end();

    return image.save(target, "PNG");
}

bool Thumbnailer::renderMediaPlate(const PackageInfo &info, const QString &target) const
{
    // Last resort for media we could not decode: a plate that at least says
    // which kind of thing it is.
    QImage image(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(kBase);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor accent = info.kind == PackageKind::Video ? kBlue : kGreen;
    const QRectF plate(16, 16, kSize - 32, kSize - 32);
    QPainterPath rounded;
    rounded.addRoundedRect(plate, 6, 6);
    painter.fillPath(rounded, kSurface);
    painter.setPen(QPen(accent, 1.5));
    painter.drawPath(rounded);

    painter.setBrush(accent);
    painter.setPen(Qt::NoPen);
    if (info.kind == PackageKind::Video) {
        QPolygonF play;
        play << QPointF(plate.center().x() - 14, plate.center().y() - 18)
             << QPointF(plate.center().x() + 20, plate.center().y())
             << QPointF(plate.center().x() - 14, plate.center().y() + 18);
        painter.drawPolygon(play);
    } else {
        painter.drawEllipse(plate.center() + QPointF(-16, 18), 11.0, 9.0);
        painter.drawRect(QRectF(plate.center().x() - 6, plate.center().y() - 26, 4, 46));
        painter.drawRect(QRectF(plate.center().x() - 6, plate.center().y() - 26, 30, 8));
    }
    painter.end();

    return image.save(target, "PNG");
}

void Thumbnailer::enqueueVideo(const QString &localPath, const QString &target)
{
    for (const VideoJob &job : std::as_const(m_videoQueue)) {
        if (job.localPath == localPath)
            return;
    }
    m_videoQueue.enqueue(VideoJob { localPath, target });
    startNextVideo();
}

void Thumbnailer::startNextVideo()
{
    if (m_videoBusy || m_videoQueue.isEmpty())
        return;
    m_videoBusy = true;
    m_grabber->grab(m_videoQueue.head().localPath);
}

void Thumbnailer::finishVideo(const QString &localPath, const QString &target, bool ok)
{
    m_videoBusy = false;

    if (!ok) {
        // The decoder could not help; draw something rather than leave a gap.
        PackageInfo placeholder;
        placeholder.kind = PackageKind::Video;
        placeholder.localPath = localPath;
        if (renderMediaPlate(placeholder, target))
            ok = true;
    }

    if (ok)
        emit thumbnailReady(localPath, QUrl::fromLocalFile(target).toString());

    startNextVideo();
}

} // namespace vsp

#include "Thumbnailer.moc"
