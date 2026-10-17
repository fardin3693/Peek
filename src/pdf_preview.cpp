#include "pdf_preview.h"

#include <QFile>

#include <cmath>
#include <memory>
#include <string>

#include <poppler-document.h>
#include <poppler-image.h>
#include <poppler-page-renderer.h>
#include <poppler-page.h>

namespace
{

// Longest rendered side, in pixels. Matches the image decode cap so PDF and
// image previews share one memory bound (~16 MiB for ARGB32).
constexpr int kMaxRenderPixels = 2048;
// DPI working range. The 2048 px cap is enforced separately and always wins;
// these bounds only keep typical pages in a sane quality range.
constexpr double kBaseDpi = 72.0;
constexpr double kMaxDpi = 300.0;
// Page boxes larger than this (in points) are rejected before any allocation
// arithmetic runs.
constexpr double kMaxPagePoints = 144000.0;

PdfPageResult renderPageFromDocument(poppler::document *doc, int pageIndex, int pageCount)
{
    PdfPageResult result;
    result.pageCount = pageCount;
    if (pageIndex < 0 || pageIndex >= pageCount) {
        result.error = QStringLiteral("page out of range");
        return result;
    }
    std::unique_ptr<poppler::page> page(doc->create_page(pageIndex));
    if (!page) {
        result.error = pageIndex == 0 ? QStringLiteral("first page is missing")
                                      : QStringLiteral("page %1 is missing").arg(pageIndex + 1);
        return result;
    }
    const poppler::rectf box = page->page_rect(poppler::crop_box);
    const double pageWidth = box.width();
    const double pageHeight = box.height();
    if (!std::isfinite(pageWidth) || !std::isfinite(pageHeight) || pageWidth <= 0.0 ||
        pageHeight <= 0.0 || pageWidth > kMaxPagePoints || pageHeight > kMaxPagePoints) {
        result.error = QStringLiteral("invalid page size");
        return result;
    }
    result.pageSizePoints = QSizeF(pageWidth, pageHeight);

    // Pick the resolution from the page aspect ratio so Poppler allocates at
    // most kMaxRenderPixels on the longest side. Verified again below, before
    // any pixel buffer exists.
    const double longest = pageWidth > pageHeight ? pageWidth : pageHeight;
    double dpi = kBaseDpi * static_cast<double>(kMaxRenderPixels) / longest;
    if (!std::isfinite(dpi) || dpi <= 0.0) {
        result.error = QStringLiteral("invalid page size");
        return result;
    }
    if (dpi > kMaxDpi) {
        dpi = kMaxDpi;
    }
    const double expectedW = pageWidth * dpi / kBaseDpi;
    const double expectedH = pageHeight * dpi / kBaseDpi;
    if (!std::isfinite(expectedW) || !std::isfinite(expectedH)) {
        result.error = QStringLiteral("invalid page size");
        return result;
    }
    const double expectedLongest = expectedW > expectedH ? expectedW : expectedH;
    if (expectedLongest > static_cast<double>(kMaxRenderPixels)) {
        dpi *= static_cast<double>(kMaxRenderPixels) / expectedLongest;
    }
    const long long pixelW = static_cast<long long>(std::ceil(pageWidth * dpi / kBaseDpi));
    const long long pixelH = static_cast<long long>(std::ceil(pageHeight * dpi / kBaseDpi));
    if (pixelW < 1 || pixelH < 1 || pixelW > kMaxRenderPixels || pixelH > kMaxRenderPixels) {
        result.error = QStringLiteral("page too large to preview");
        return result;
    }

    poppler::page_renderer renderer;
    renderer.set_image_format(poppler::image::format_argb32);
    const poppler::image rendered = renderer.render_page(page.get(), dpi, dpi);
    if (!rendered.is_valid() || rendered.format() != poppler::image::format_argb32) {
        result.error = pageIndex == 0
                           ? QStringLiteral("could not rasterize the first page")
                           : QStringLiteral("could not rasterize page %1").arg(pageIndex + 1);
        return result;
    }
    const int width = rendered.width();
    const int height = rendered.height();
    const char *data = rendered.const_data();
    const int stride = rendered.bytes_per_row();
    const long long minStride = static_cast<long long>(width) * 4LL;
    // Width/height fit int here (bounded above), but stride arithmetic stays
    // 64-bit so hostile values cannot overflow the buffer-size check.
    const long long bufferBytes = static_cast<long long>(stride) * static_cast<long long>(height);
    constexpr long long kMaxBufferBytes =
        (static_cast<long long>(kMaxRenderPixels) * 4LL + 1024LL) *
        static_cast<long long>(kMaxRenderPixels);
    if (width < 1 || height < 1 || width > kMaxRenderPixels || height > kMaxRenderPixels ||
        data == nullptr || stride < minStride || bufferBytes <= 0 ||
        bufferBytes > kMaxBufferBytes) {
        result.error = pageIndex == 0
                           ? QStringLiteral("could not rasterize the first page")
                           : QStringLiteral("could not rasterize page %1").arg(pageIndex + 1);
        return result;
    }

    // QImage only aliases the buffer; copy() detaches so the image outlives
    // the Poppler objects destroyed at scope exit.
    const QImage view(reinterpret_cast<const uchar *>(data), width, height, stride,
                      QImage::Format_ARGB32);
    result.image = view.copy();
    if (result.image.isNull()) {
        result.error = pageIndex == 0
                           ? QStringLiteral("could not rasterize the first page")
                           : QStringLiteral("could not rasterize page %1").arg(pageIndex + 1);
        return result;
    }
    return result;
}

} // namespace

bool isPdfFile(const QString &path)
{
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) {
        return false;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(5) == QByteArray("%PDF-", 5);
}

PdfDocumentInfo pdfDocumentInfo(const QString &path)
{
    PdfDocumentInfo info;
    const std::string localPath = QFile::encodeName(path).toStdString();
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(localPath));
    if (!doc) {
        info.error = QStringLiteral("unreadable or corrupt");
        return info;
    }
    if (doc->is_locked()) {
        info.error = QStringLiteral("password-protected (password required)");
        return info;
    }
    const int pageCount = doc->pages();
    if (pageCount < 1) {
        info.error = QStringLiteral("no pages");
        return info;
    }
    info.pageCount = pageCount;
    return info;
}

PdfPageResult renderPdfPage(const QString &path, int pageIndex)
{
    const std::string localPath = QFile::encodeName(path).toStdString();
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(localPath));
    if (!doc) {
        PdfPageResult result;
        result.error = QStringLiteral("unreadable or corrupt");
        return result;
    }
    if (doc->is_locked()) {
        PdfPageResult result;
        result.error = QStringLiteral("password-protected (password required)");
        return result;
    }
    const int pageCount = doc->pages();
    if (pageCount < 1) {
        PdfPageResult result;
        result.error = QStringLiteral("no pages");
        return result;
    }
    return renderPageFromDocument(doc.get(), pageIndex, pageCount);
}

PdfFirstPageResult renderPdfFirstPage(const QString &path)
{
    PdfFirstPageResult result;
    const PdfPageResult page = renderPdfPage(path, 0);
    result.pageCount = page.pageCount;
    result.image = page.image;
    result.error = page.error;
    return result;
}
