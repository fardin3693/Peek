#include "video_preview.h"

#include <QFileInfo>
#include <QSet>

bool isVideoFile(const QString &path)
{
    static const QSet<QString> extensions = {
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("m4v"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
