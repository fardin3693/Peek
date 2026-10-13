#include "audio_preview.h"

#include <QFileInfo>
#include <QSet>

bool isAudioFile(const QString &path)
{
    static const QSet<QString> extensions = {
        QStringLiteral("mp3"), QStringLiteral("wav"),  QStringLiteral("flac"),
        QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("m4a"),
        QStringLiteral("aac"), QStringLiteral("wma"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
