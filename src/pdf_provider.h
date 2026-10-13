#pragma once

#include "preview_provider.h"

// Third concrete preview provider: PDF first page (see src/pdf_preview.h).
//
// Identification is the existing cheap, bounded check from isPdfFile(): a
// case-insensitive ".pdf" suffix plus a 5-byte "%PDF-" magic read. No page
// is rendered merely to identify a file. Rendering is the extracted
// PreviewWindow::showPdfFile body with identical messages, object names,
// and sizing policy. No new dependencies (Poppler, via the existing
// PEEK_WITH_PDF backend).
//
// This header and its implementation are only built into targets that
// define PEEK_WITH_PDF; see CMakeLists.txt.
ProviderSupport pdfProviderSupports(const QString &path);
bool renderPdfPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &pdfProvider();
