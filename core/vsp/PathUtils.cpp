#include "PathUtils.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

#include <cmath>

namespace vsp::path {
namespace {

// Windows reserves these regardless of extension; harmless to filter elsewhere.
bool isReservedDeviceName(const QString &stem)
{
    static const QStringList kReserved = {
        QStringLiteral("CON"),  QStringLiteral("PRN"),  QStringLiteral("AUX"),
        QStringLiteral("NUL"),  QStringLiteral("COM1"), QStringLiteral("COM2"),
        QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("COM5"),
        QStringLiteral("COM6"), QStringLiteral("COM7"), QStringLiteral("COM8"),
        QStringLiteral("COM9"), QStringLiteral("LPT1"), QStringLiteral("LPT2"),
        QStringLiteral("LPT3"), QStringLiteral("LPT4"), QStringLiteral("LPT5"),
        QStringLiteral("LPT6"), QStringLiteral("LPT7"), QStringLiteral("LPT8"),
        QStringLiteral("LPT9")
    };
    return kReserved.contains(stem.toUpper());
}

} // namespace

bool isValidIPv4(const QString &address)
{
    const QString trimmed = address.trimmed();
    const QStringList octets = trimmed.split(QLatin1Char('.'));
    if (octets.size() != 4)
        return false;

    for (const QString &octet : octets) {
        if (octet.isEmpty() || octet.size() > 3)
            return false;
        for (const QChar c : octet) {
            if (!c.isDigit() || c.unicode() > 0x7f)
                return false;
        }
        // "01" and "007" are ambiguous (some resolvers read them as octal).
        if (octet.size() > 1 && octet.startsWith(QLatin1Char('0')))
            return false;
        if (octet.toInt() > 255)
            return false;
    }
    return true;
}

bool isValidPort(int port)
{
    return port > 0 && port <= 65535;
}

QString sanitizeFileName(const QString &name)
{
    QString out;
    out.reserve(name.size());

    for (const QChar c : name) {
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7f)              // control characters
            continue;
        if (c == QLatin1Char('/') || c == QLatin1Char('\\'))
            continue;
        if (QStringLiteral("<>:\"|?*").contains(c))
            continue;
        out.append(c);
    }

    // A component made only of dots is "." or ".." or a hidden-traversal trick.
    while (out.startsWith(QLatin1Char('.')))
        out.remove(0, 1);
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);

    out = out.trimmed();

    if (isReservedDeviceName(QFileInfo(out).completeBaseName()))
        out.prepend(QStringLiteral("_"));

    if (out.isEmpty())
        out = QStringLiteral("unnamed");

    // Vita's filesystem tops out well below this; keep both ends happy.
    constexpr int kMaxComponent = 200;
    if (out.size() > kMaxComponent)
        out = out.left(kMaxComponent);

    return out;
}

bool isSafeComponent(const QString &name)
{
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')))
        return false;
    for (const QChar c : name) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f)
            return false;
    }
    return true;
}

QString normalizeRemote(const QString &remotePath)
{
    QString working = remotePath;
    working.replace(QLatin1Char('\\'), QLatin1Char('/'));

    QStringList resolved;
    const QStringList parts = working.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part == QLatin1String("."))
            continue;
        if (part == QLatin1String("..")) {
            if (!resolved.isEmpty())
                resolved.removeLast();
            continue;                            // at the root ".." is a no-op
        }
        resolved.append(part);
    }

    if (resolved.isEmpty())
        return QStringLiteral("/");
    return QLatin1Char('/') + resolved.join(QLatin1Char('/'));
}

QString joinRemote(const QString &base, const QString &leaf)
{
    const QString safeLeaf = sanitizeFileName(leaf);
    const QString normalizedBase = normalizeRemote(base);
    if (normalizedBase == QLatin1String("/"))
        return QLatin1Char('/') + safeLeaf;
    return normalizedBase + QLatin1Char('/') + safeLeaf;
}

QString parentOfRemote(const QString &remotePath)
{
    const QString normalized = normalizeRemote(remotePath);
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("/");
    return normalized.left(slash);
}

QString baseNameOfRemote(const QString &remotePath)
{
    const QString normalized = normalizeRemote(remotePath);
    const int slash = normalized.lastIndexOf(QLatin1Char('/'));
    return normalized.mid(slash + 1);
}

bool isMountRoot(const QString &remotePath)
{
    const QString normalized = normalizeRemote(remotePath);
    static const QRegularExpression re(QStringLiteral("^/[a-z]{2}[0-9]:$"));
    return re.match(normalized).hasMatch();
}

QString mountOf(const QString &remotePath)
{
    const QString normalized = normalizeRemote(remotePath);
    const QStringList parts = normalized.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return {};
    return parts.first().endsWith(QLatin1Char(':')) ? parts.first() : QString();
}

QString safeLocalTarget(const QString &localDir, const QString &untrustedName)
{
    const QString component = sanitizeFileName(untrustedName);
    const QDir dir(localDir);
    const QString candidate = QDir::cleanPath(dir.absoluteFilePath(component));
    const QString root = QDir::cleanPath(dir.absolutePath());

    // Belt and braces: the sanitiser already stripped separators, but confirm
    // the resolved path really is a child before we ever open it for writing.
    if (candidate == root)
        return {};
    if (!candidate.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/')))
        return {};
    return candidate;
}

QString humanSize(qint64 bytes)
{
    if (bytes < 0)
        return QStringLiteral("--");
    if (bytes < 1024)
        return QStringLiteral("%1 B").arg(bytes);

    static const char *units[] = { "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes) / 1024.0;
    int unit = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }
    return QStringLiteral("%1 %2")
        .arg(value, 0, 'f', value >= 100.0 ? 0 : 1)
        .arg(QLatin1String(units[unit]));
}

QString humanRate(double bytesPerSecond)
{
    if (!(bytesPerSecond > 0.0) || std::isnan(bytesPerSecond))
        return QStringLiteral("--");
    return humanSize(static_cast<qint64>(bytesPerSecond)) + QStringLiteral("/s");
}

QString collapseUserHome(const QString &absolutePath)
{
    const QString home = QDir::cleanPath(QDir::homePath());
    const QString cleaned = QDir::cleanPath(absolutePath);
    if (cleaned != home && !cleaned.startsWith(home + QLatin1Char('/')))
        return absolutePath;
    return QLatin1Char('~') + cleaned.mid(home.size());
}

QString expandUserHome(const QString &pathOrDisplay)
{
    if (pathOrDisplay == QLatin1String("~"))
        return QDir::homePath();
    if (pathOrDisplay.startsWith(QLatin1String("~/")))
        return QDir::homePath() + pathOrDisplay.mid(1);
    return pathOrDisplay;
}

} // namespace vsp::path
