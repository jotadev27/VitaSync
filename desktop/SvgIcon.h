#pragma once

#include <QQuickPaintedItem>
#include <QSvgRenderer>

// Render the upstream SVG at the display's pixel density with supersampling.
class SvgIcon : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString svg READ svg WRITE setSvg NOTIFY svgChanged)

public:
    explicit SvgIcon(QQuickItem *parent = nullptr);
    QString svg() const { return m_svg; }
    void setSvg(const QString &svg);
    void paint(QPainter *painter) override;

signals:
    void svgChanged();

private:
    void resizeTexture();
    QString m_svg;
    QSvgRenderer m_renderer;
};
