// Build with Qt Gui and Svg, then pass the project root to render both bitmaps.
#include <QGuiApplication>
#include <QSvgRenderer>
#include <QPainter>
#include <QImage>
#include <QDir>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString root = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    for (const auto &name : {QStringLiteral("wizard"), QStringLiteral("header")}) {
        const QString base = root + "/assets/installer/" + name;
        QSvgRenderer layout(base + ".svg");
        if (!layout.isValid()) return 1;
        QImage image(layout.defaultSize(), QImage::Format_RGB32);
        image.fill(Qt::white);
        {
            QPainter painter(&image);
            painter.setRenderHint(QPainter::Antialiasing);
            layout.render(&painter);
            if (name == QLatin1String("wizard")) {
                QSvgRenderer vita(root + "/assets/installer/vita.svg");
                if (!vita.isValid()) return 1;
                const QSize size = vita.defaultSize();
                const qreal height = 300.0 * size.height() / size.width();
                vita.render(&painter, QRectF(14, 250, 300, height));
            }
        }
        if (!image.save(base + ".bmp")) return 1;
    }
    return 0;
}
