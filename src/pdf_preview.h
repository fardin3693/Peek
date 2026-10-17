#pragma once

#include <QImage>
#include <QSizeF>
#include <QString>

// Multi-page PDF rendering, backed by the Poppler core C++ API.
// This header is only compiled into targets that define PEEK_WITH_PDF.

struct PdfFirstPageResult {
    QImage image; // Null when rendering failed; see error.
    int pageCount = 0;
    QString error; // Empty on success.
};

// Document identity without rendering: page count for navigation plus the
// existing locked/corrupt/empty reasons. Cheap (no rasterization) and used
// to initialise the preview before the first page image arrives.
struct PdfDocumentInfo {
    int pageCount = 0;
    QString error; // Empty on success.
};

// On-demand single-page render. The worker thread renders exactly one
// requested page per task and discards the document immediately, so zooming
// (pure display scaling) never reloads the document and navigation never
// renders eagerly. Same 2048 px / 300 DPI bounds as the first-page path.
struct PdfPageResult {
    QImage image; // Null on failure; see error.
    QSizeF pageSizePoints;
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

// Opens the document only to count pages and detect locked/corrupt/empty
// files. Never renders. Used for the page indicator and initial selection.
PdfDocumentInfo pdfDocumentInfo(const QString &path);

// Renders page `pageIndex` (0-based) with the same bounds as the first-page
// path. Out-of-range pages yield an error, never a crash. Thread-safe when
// each calling thread uses its own document instance (the implementation
// opens, renders one page, and closes without shared state).
PdfPageResult renderPdfPage(const QString &path, int pageIndex);
