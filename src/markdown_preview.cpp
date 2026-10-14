#include "markdown_preview.h"

#include <QFileInfo>
#include <QSet>

bool isMarkdownFile(const QString &path)
{
    static const QSet<QString> extensions = {
        QStringLiteral("md"),
        QStringLiteral("markdown"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
