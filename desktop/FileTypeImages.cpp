#include "FileTypeImages.h"

#include <QIcon>
#include <QMutexLocker>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <knownfolders.h>
#endif

namespace {
QImage trimTransparentBorder(const QImage &source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(line[x]) > 8) bounds |= QRect(x, y, 1, 1);
    }
    return bounds.isEmpty() ? source : source.copy(bounds.adjusted(-2, -2, 2, 2).intersected(source.rect()));
}

QImage categoryImage(const QString &kind)
{
#ifdef Q_OS_WIN
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    SHSTOCKICONID stock = SIID_DOCNOASSOC;
    if (kind == QLatin1String("folder") || kind == QLatin1String("download")) stock = SIID_FOLDER;
    else if (kind == QLatin1String("photo") || kind == QLatin1String("theme")) stock = SIID_IMAGEFILES;
    else if (kind == QLatin1String("video")) stock = SIID_VIDEOFILES;
    else if (kind == QLatin1String("music")) stock = SIID_AUDIOFILES;
    else if (kind == QLatin1String("archive")) stock = SIID_ZIPFILE;
    else if (kind == QLatin1String("package") || kind == QLatin1String("android")) stock = SIID_SOFTWARE;
    SHSTOCKICONINFO info{};
    info.cbSize = sizeof(info);
    int index = -1;
    if (SUCCEEDED(SHGetStockIconInfo(stock, SHGSI_SYSICONINDEX, &info))) index = info.iSysImageIndex;
    if (kind == QLatin1String("download")) {
        PIDLIST_ABSOLUTE location = nullptr;
        // Resolve only the known-folder icon; never enumerate folder contents.
        if (SUCCEEDED(SHGetKnownFolderIDList(FOLDERID_Downloads, KF_FLAG_DONT_VERIFY, nullptr, &location))) {
            SHFILEINFOW folder{};
            if (SHGetFileInfoW(reinterpret_cast<LPCWSTR>(location), 0, &folder, sizeof(folder),
                               SHGFI_PIDL | SHGFI_SYSICONINDEX)) index = folder.iIcon;
            CoTaskMemFree(location);
        }
    }
    QImage image;
    IImageList *images = nullptr;
    if (index >= 0 && SUCCEEDED(SHGetImageList(SHIL_JUMBO, __uuidof(IImageList), reinterpret_cast<void **>(&images)))) {
        HICON icon = nullptr;
        if (SUCCEEDED(images->GetIcon(index, ILD_TRANSPARENT, &icon))) {
            image = QImage::fromHICON(icon);
            DestroyIcon(icon);
        }
        images->Release();
    }
    if (image.isNull() && SUCCEEDED(SHGetStockIconInfo(stock, SHGSI_ICON | SHGSI_LARGEICON, &info))) {
        image = QImage::fromHICON(info.hIcon);
        DestroyIcon(info.hIcon);
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
    return trimTransparentBorder(image);
#else
    QString name = QStringLiteral("application-octet-stream");
    if (kind == QLatin1String("folder")) name = QStringLiteral("folder");
    else if (kind == QLatin1String("photo")) name = QStringLiteral("image-x-generic");
    else if (kind == QLatin1String("video")) name = QStringLiteral("video-x-generic");
    else if (kind == QLatin1String("music")) name = QStringLiteral("audio-x-generic");
    else if (kind == QLatin1String("download")) name = QStringLiteral("folder-download");
    else if (kind == QLatin1String("archive")) name = QStringLiteral("package-x-generic");
    return QIcon::fromTheme(name).pixmap(256, 256).toImage();
#endif
}
}

FileTypeImages::FileTypeImages() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage FileTypeImages::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QString kind = id.section(QLatin1Char('/'), 0, 0).toLower();
    QMutexLocker lock(&m_mutex);
    if (!m_images.contains(kind)) m_images.insert(kind, categoryImage(kind));
    QImage result = m_images.value(kind);
    if (result.isNull()) result.load(QStringLiteral(":/qt/qml/VitaSync/Ui/brand/appicon.png"));
    if (size) *size = result.size();
    if (requestedSize.isValid())
        result = result.scaled(requestedSize.boundedTo(QSize(256, 256)), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return result;
}
