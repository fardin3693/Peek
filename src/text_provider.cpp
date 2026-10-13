#include "text_provider.h"

#include "text_preview.h"

#include <QFontDatabase>
#include <QFontMetrics>
#include <QLabel>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QWidget>

ProviderSupport textProviderSupports(const QString &path)
{
    return isTextFile(path) ? ProviderSupport::Yes : ProviderSupport::No;
}

bool renderTextPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
    const TextPreviewResult text = loadTextPreview(path);
    if (!text.error.isEmpty()) {
        statusLabel->setText(
            QStringLiteral("Could not load a text preview of this file (%1).").arg(text.error));
        return false;
    }
    if (text.text.isEmpty()) {
        statusLabel->setText(QStringLiteral("Text file — empty (%1 bytes)").arg(text.byteCount));
    } else {
        statusLabel->setText(QStringLiteral("Text file — %1 lines — %2 bytes")
                                 .arg(text.lineCount)
                                 .arg(text.byteCount));
    }

    // Parent views to the host widget owning the layout so Qt destroys them
    // with the window; the dispatch layer tracks them via object names.
    QWidget *host = layout->parentWidget();
    if (host == nullptr) {
        host = qobject_cast<QWidget *>(statusLabel->parent());
    }
    auto *textView = new QPlainTextEdit(host);
    textView->setObjectName(QStringLiteral("textView"));
    textView->setReadOnly(true);
    // Long lines scroll horizontally instead of forcing a wide window.
    textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    textView->setFont(fixedFont);
    textView->setTabStopDistance(QFontMetricsF(fixedFont).horizontalAdvance(QLatin1Char(' ')) *
                                 4.0);
    textView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    textView->setMinimumSize(160, 120);
    // Plain text only: never interpreted as rich text or HTML.
    textView->setPlainText(text.text);
    layout->addWidget(textView, 1);
    return true;
}

const PreviewProvider &textProvider()
{
    static const PreviewProvider provider = {"text", &textProviderSupports, &renderTextPreview};
    return provider;
}
