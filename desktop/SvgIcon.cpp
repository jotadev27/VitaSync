#include "SvgIcon.h"

#include <QPainter>
#include <QtMath>

SvgIcon::SvgIcon(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setMipmap(true);
    setOpaquePainting(false);
    setFillColor(Qt::transparent);
    setRenderTarget(QQuickPaintedItem::Image);
    connect(this, &QQuickItem::widthChanged, this, &SvgIcon::resizeTexture);
    connect(this, &QQuickItem::heightChanged, this, &SvgIcon::resizeTexture);
}

void SvgIcon::resizeTexture()
{
    // Qt additionally applies the window's device pixel ratio to this size.
    setTextureSize(QSize(qMax(1, qCeil(width() * 4)), qMax(1, qCeil(height() * 4))));
    update();
}

void SvgIcon::setSvg(const QString &svg)
{
    if (m_svg == svg)
        return;
    m_svg = svg;
    m_renderer.load(svg.toUtf8());
    m_renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    update();
    emit svgChanged();
}

void SvgIcon::paint(QPainter *painter)
{
    if (!m_renderer.isValid())
        return;
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    const qreal side = qMin(width(), height());
    m_renderer.render(painter, QRectF((width() - side) / 2, (height() - side) / 2, side, side));
}
