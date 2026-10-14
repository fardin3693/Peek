#include "markdown_provider.h"

#include "markdown_preview.h"
#include "text_preview.h"

#include <QLabel>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

// Read-only Markdown view that never fetches remote content and never
// navigates. Image/stylesheet resources resolve to nothing (so `![alt](url)`
// leaves a placeholder instead of a network fetch), and with openLinks
// disabled anchor clicks emit anchorClicked without following the link —
// with no connection attached, that signal goes nowhere. Note: an empty URL
// passes isLocalFile()/scheme checks but QTextBrowser cannot resolve it to
// content, so it still renders as a placeholder.
class MarkdownView : public QTextBrowser
{
  public:
    explicit MarkdownView(QWidget *parent = nullptr) : QTextBrowser(parent) {}

    QVariant loadResource(int type, const QUrl &name) override
    {
        // Block remote image/stylesheet loads: only allow the local-file
        // fallback for file: URLs so relative images next to the document can
        // still render when already on disk. Everything else (http/https,
        // data:, unknown schemes) resolves empty.
        if (type == QTextDocument::ImageResource || type == QTextDocument::StyleSheetResource) {
            if (name.isLocalFile() || name.scheme().isEmpty()) {
                return QTextBrowser::loadResource(type, name);
            }
            return {};
        }
        return QTextBrowser::loadResource(type, name);
    }
};

} // namespace

ProviderSupport markdownProviderSupports(const QString &path)
{
    return isMarkdownFile(path) ? ProviderSupport::Yes : ProviderSupport::No;
}

bool renderMarkdownPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
    // Reuse the bounded, strict-UTF-8 text load (2 MiB cap, BOM strip, NUL
    // and invalid-UTF-8 rejection), so Markdown inherits the same resource
    // limits and malformed-input handling as plain text.
    const TextPreviewResult text = loadTextPreview(path);
    if (!text.error.isEmpty()) {
        statusLabel->setText(
            QStringLiteral("Could not load a Markdown preview of this file (%1).").arg(text.error));
        return false;
    }
    if (text.text.isEmpty()) {
        statusLabel->setText(
            QStringLiteral("Markdown file — empty (%1 bytes)").arg(text.byteCount));
    } else {
        statusLabel->setText(QStringLiteral("Markdown file — %1 lines — %2 bytes")
                                 .arg(text.lineCount)
                                 .arg(text.byteCount));
    }

    // Parent views to the host widget owning the layout so Qt destroys them
    // with the window; the dispatch layer tracks them via object names.
    QWidget *host = layout->parentWidget();
    if (host == nullptr) {
        host = qobject_cast<QWidget *>(statusLabel->parent());
    }
    auto *markdownView = new MarkdownView(host);
    markdownView->setObjectName(QStringLiteral("markdownView"));
    markdownView->setReadOnly(true);
    // Never navigate or fetch: links stay inert, external opens stay off, and
    // there is no search path for relative source lookups.
    markdownView->setOpenLinks(false);
    markdownView->setOpenExternalLinks(false);
    markdownView->setSearchPaths(QStringList());
    markdownView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    markdownView->setMinimumSize(160, 120);
    QTextDocument::MarkdownFeatures features = QTextDocument::MarkdownDialectGitHub;
    features.setFlag(QTextDocument::MarkdownNoHTML);
    markdownView->document()->setMarkdown(text.text, features);
    layout->addWidget(markdownView, 1);
    return true;
}

const PreviewProvider &markdownProvider()
{
    static const PreviewProvider provider = {"markdown", &markdownProviderSupports,
                                             &renderMarkdownPreview};
    return provider;
}
