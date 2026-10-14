#include "markdown_provider.h"

#include "markdown_preview.h"
#include "preview_util.h"
#include "text_preview.h"

#include <QDir>
#include <QFileInfo>
#include <QLabel>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

namespace
{

// Read-only Markdown view that never fetches remote content and never
// navigates. Image/stylesheet resources resolve only to files inside the
// document directory (relative URLs resolve against the document, never the
// process working directory; absolute file: URLs outside the document map to
// a placeholder), and images decode through the shared 2048 px bounded
// loader. With openLinks disabled anchor clicks emit anchorClicked without
// following the link — with no connection attached, that signal goes nowhere.
class MarkdownView : public QTextBrowser
{
  public:
    explicit MarkdownView(const QString &documentDir, QWidget *parent = nullptr)
        : QTextBrowser(parent), m_baseDir(documentDir)
    {
    }

    QVariant loadResource(int type, const QUrl &name) override
    {
        if (type == QTextDocument::ImageResource) {
            const QString local = peek_util::resolveSiblingResource(name, m_baseDir);
            if (local.isEmpty()) {
                return {};
            }
            const QImage bounded = peek_util::loadBoundedImage(local);
            if (bounded.isNull()) {
                return {};
            }
            return QVariant(bounded);
        }
        if (type == QTextDocument::StyleSheetResource) {
            // Stylesheets carry no decode risk, but the same directory scope
            // applies: only siblings of the document may load, resolved
            // explicitly so the cwd never leaks in.
            const QString local = peek_util::resolveSiblingResource(name, m_baseDir);
            if (local.isEmpty()) {
                return {};
            }
            return QTextBrowser::loadResource(type, QUrl::fromLocalFile(local));
        }
        return QTextBrowser::loadResource(type, name);
    }

  private:
    QString m_baseDir;
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
    QWidget *host = peek_util::hostWidget(layout, statusLabel);
    const QString documentDir = QFileInfo(path).absolutePath();
    auto *markdownView = new MarkdownView(documentDir, host);
    markdownView->setObjectName(QStringLiteral("markdownView"));
    markdownView->setReadOnly(true);
    // Never navigate or fetch: links stay inert, external opens stay off, and
    // relative lookups resolve against the document directory (set here so
    // any base-class fallback cannot fall back to the process cwd).
    markdownView->setOpenLinks(false);
    markdownView->setOpenExternalLinks(false);
    markdownView->setSearchPaths(QStringList(documentDir));
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
