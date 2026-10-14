#pragma once

#include "preview_provider.h"

// Markdown preview provider (see src/markdown_preview.h).
//
// Identification is the existing cheap, bounded check from isMarkdownFile():
// a case-insensitive extension match (md, markdown). No file content is read
// or decoded merely to identify a file. Rendering reuses the bounded,
// strict-UTF-8 load from loadTextPreview() and renders through Qt's built-in
// Markdown support (QTextDocument::setMarkdown, GitHub dialect with
// MarkdownNoHTML) into a read-only text view with navigation disabled, so raw
// HTML in the source is shown escaped and links/images never fetch or open.
// Images load only from inside the document directory (relative URLs resolve
// against the document, never the cwd; absolute file: URLs outside it map to
// a placeholder) through the shared 2048 px bounded decoder. A Markdown
// claim ends dispatch even when the file cannot be rendered, so invalid
// Markdown never falls through to an unrelated preview.
ProviderSupport markdownProviderSupports(const QString &path);
bool renderMarkdownPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &markdownProvider();
