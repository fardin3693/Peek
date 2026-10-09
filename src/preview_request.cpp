#include "preview_request.h"

#include <QDir>
#include <QFileInfo>

PreviewRequestResult validatePreviewPath(const QString &path)
{
    if (path.isEmpty()) {
        return {{}, QStringLiteral("empty path")};
    }

    // Do not clean "..": a symlink preceding it can make lexical cleanup
    // point at a different object than the path the user requested.
    QString absolutePath = path;
    // Linux absolute paths start with '/'; Qt also treats ':' as a resource
    // prefix, but it is an ordinary filename character for this filesystem CLI.
    if (!path.startsWith(QLatin1Char('/'))) {
        QString base = QDir::currentPath();
        if (!base.endsWith(QLatin1Char('/'))) {
            base += QLatin1Char('/');
        }
        absolutePath = base + path;
    }
    const QFileInfo info(absolutePath);
    if (!info.exists()) {
        return {{},
                QStringLiteral("path does not exist or cannot be accessed: %1").arg(absolutePath)};
    }
    if (!info.isFile() && !info.isDir()) {
        return {{}, QStringLiteral("unsupported file type: %1").arg(absolutePath)};
    }

    return {{absolutePath, info.isDir()}, {}};
}
