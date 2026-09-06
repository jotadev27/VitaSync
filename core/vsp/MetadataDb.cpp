#include "MetadataDb.h"

#include "PathUtils.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUrl>

namespace vsp {
namespace {

const QStringList kCoverExtensions = {
    QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp")
};

QString normalizeTitleId(const QString &titleId)
{
    return titleId.trimmed().toUpper();
}

} // namespace

MetadataDb::MetadataDb(QObject *parent)
    : QObject(parent)
{
}

bool MetadataDb::load(const QString &indexPath)
{
    QFile file(indexPath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError parseError {};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();

    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    const QJsonObject titles = root.value(QStringLiteral("titles")).toObject();

    m_titles.clear();
    m_titles.reserve(titles.size());

    for (auto it = titles.constBegin(); it != titles.constEnd(); ++it) {
        const QString titleId = normalizeTitleId(it.key());
        if (titleId.isEmpty())
            continue;

        TitleRecord record;
        record.titleId = titleId;

        // Two accepted shapes: a bare name string, or an object with extras.
        if (it.value().isString()) {
            record.name = it.value().toString();
        } else {
            const QJsonObject entry = it.value().toObject();
            record.name      = entry.value(QStringLiteral("name")).toString();
            record.region    = entry.value(QStringLiteral("region")).toString();
            record.publisher = entry.value(QStringLiteral("publisher")).toString();
        }

        if (record.name.isEmpty())
            continue;
        m_titles.insert(titleId, record);
    }

    // Art shipped next to the index, if any, is always searched first.
    const QString bundledCovers = QFileInfo(indexPath).absolutePath() + QStringLiteral("/art");
    m_coverRoots.clear();
    if (QFileInfo::exists(bundledCovers))
        m_coverRoots.append(bundledCovers);

    m_covers.clear();
    for (const QString &root : std::as_const(m_coverRoots))
        indexCoverDirectory(root);
    if (!m_coverPackPath.isEmpty())
        indexCoverDirectory(m_coverPackPath);

    emit changed();
    return true;
}

void MetadataDb::setCoverPackPath(const QString &directory)
{
    const QString cleaned = QDir::cleanPath(directory);
    if (cleaned == m_coverPackPath)
        return;

    m_coverPackPath = cleaned;

    m_covers.clear();
    for (const QString &root : std::as_const(m_coverRoots))
        indexCoverDirectory(root);
    if (!m_coverPackPath.isEmpty())
        indexCoverDirectory(m_coverPackPath);

    emit changed();
}

void MetadataDb::indexCoverDirectory(const QString &directory)
{
    if (directory.isEmpty() || !QFileInfo(directory).isDir())
        return;

    // Cover packs are flat folders of TITLEID.png, but some nest by region, so
    // walk a couple of levels rather than insisting on one shape.
    QDirIterator it(directory,
                    QDir::Files | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);

    int guard = 0;
    while (it.hasNext() && guard < 100000) {
        const QString filePath = it.next();
        ++guard;

        const QFileInfo fileInfo(filePath);
        if (!kCoverExtensions.contains(fileInfo.suffix().toLower()))
            continue;

        const QString stem = normalizeTitleId(fileInfo.completeBaseName());
        if (stem.isEmpty())
            continue;
        // First match wins, so bundled art is not shadowed by a stray file.
        if (!m_covers.contains(stem))
            m_covers.insert(stem, fileInfo.absoluteFilePath());
    }
}

bool MetadataDb::hasTitle(const QString &titleId) const
{
    return m_titles.contains(normalizeTitleId(titleId));
}

QString MetadataDb::nameFor(const QString &titleId) const
{
    return m_titles.value(normalizeTitleId(titleId)).name;
}

QString MetadataDb::coverUrlFor(const QString &titleId) const
{
    const QString path = m_covers.value(normalizeTitleId(titleId));
    if (path.isEmpty())
        return {};
    return QUrl::fromLocalFile(path).toString();
}

void MetadataDb::resolve(PackageInfo *info) const
{
    if (!info)
        return;

    const QString titleId = normalizeTitleId(info->titleId);
    if (titleId.isEmpty())
        return;

    const auto it = m_titles.constFind(titleId);
    if (it != m_titles.constEnd()) {
        info->metadataMatched = true;
        info->region = it->region;
        info->publisher = it->publisher;
        // param.sfo wins; the database only fills a gap.
        if (info->title.isEmpty())
            info->title = it->name;
    }

    const QString cover = m_covers.value(titleId);
    if (!cover.isEmpty())
        info->coverPath = cover;
}

} // namespace vsp
