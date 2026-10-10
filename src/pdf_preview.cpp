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

PdfFirstPageResult renderPdfFirstPage(const QString &path)
{
    PdfFirstPageResult result;

    const std::string localPath = QFile::encodeName(path).toStdString();
    std::unique_ptr<poppler::document> doc(poppler::document::load_from_file(localPath));
    if (!doc) {
        result.error = QStringLiteral("unreadable or corrupt");
        return result;
    }
    if (doc->is_locked()) {
        result.error = QStringLiteral("password-protected (password required)");
        return result;
    }
    const int pageCount = doc->pages();
    if (pageCount < 1) {
        result.error = QStringLiteral("no pages");
        return result;
    }
    result.pageCount = pageCount;

    std::unique_ptr<poppler::page> page(doc->create_page(0));
    if (!page) {
        result.error = QStringLiteral("first page is missing");
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
        result.error = QStringLiteral("could not rasterize the first page");
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
        result.error = QStringLiteral("could not rasterize the first page");
        return result;
    }

    // QImage only aliases the buffer; copy() detaches so the image outlives
    // the Poppler objects destroyed at scope exit.
    const QImage view(reinterpret_cast<const uchar *>(data), width, height, stride,
                      QImage::Format_ARGB32);
    result.image = view.copy();
    if (result.image.isNull()) {
        result.error = QStringLiteral("could not rasterize the first page");
        return result;
    }
    return result;
}
