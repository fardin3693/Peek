#pragma once

#include <QImage>
#include <QString>

// First-page PDF rendering, backed by the Poppler core C++ API.
// This header is only compiled into targets that define PEEK_WITH_PDF.

struct PdfFirstPageResult {
    QImage image; // Null when rendering failed; see error.
    int pageCount = 0;
    QString error; // Empty on success.
};

// True when path looks like a PDF: a ".pdf" suffix (case-insensitive) plus a
// "%PDF-" magic header at the start of the file. A bare extension never
// suffices, so misnamed files fall through to the generic fallback.
bool isPdfFile(const QString &path);

// Renders page zero (the first page) at a resolution capped so the longest
// output side is at most 2048 px. Never prompts for passwords: locked,
// corrupt, empty, or otherwise unreadable documents yield a null image with
// error set to a short human-readable reason.
PdfFirstPageResult renderPdfFirstPage(const QString &path);
