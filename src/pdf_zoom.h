#pragma once

#include <QSize>
#include <QString>

// Pure PDF zoom/fit math: no Poppler, no file access, always built so the
// calculations stay unit-testable in PEEK_WITH_PDF=OFF builds. Rendering and
// document queries live in src/pdf_preview.h (Poppler only).
namespace pdf_zoom
{

// User-zoom range for the lightweight preview. Base renders are capped at
// 2048 px (see pdf_preview.h), so 4x stays bounded and 0.25x keeps small
// pages legible. Step is a Quick Look-style 25% increment.
constexpr double kMinZoom = 0.25;
constexpr double kMaxZoom = 4.0;
constexpr double kZoomStep = 1.25;
// Bounded page-image cache: current plus neighbours, evicting the farthest.
// Three 2048px ARGB32 pages worst-case ~48 MiB, acceptable for the
// resource-constrained target; larger histories are deliberately not kept.
constexpr int kMaxCachedPages = 3;
// Longest side allowed for a zoomed/widened display pixmap. Rendered base
// pages are at most 2048 px, so an uncapped 4x manual zoom could allocate an
// 8192-px (~256 MiB ARGB32) pixmap; 4096 keeps the worst case near 64 MiB
// while still permitting a 2x upscale over the base render, past which
// raster zooming adds no detail. FitPage needs no cap (it stays inside the
// viewport); Manual and FitWidth are capped below.
constexpr int kMaxDisplayPixels = 4096;

enum class FitMode { Manual, FitPage, FitWidth };

inline double clampZoom(double zoom)
{
    if (!(zoom > 0.0)) {
        return 1.0;
    }
    if (zoom < kMinZoom) {
        return kMinZoom;
    }
    if (zoom > kMaxZoom) {
        return kMaxZoom;
    }
    return zoom;
}

inline double zoomIn(double zoom) { return clampZoom(zoom * kZoomStep); }

inline double zoomOut(double zoom) { return clampZoom(zoom / kZoomStep); }

// Shrink-only bound of a computed display size to kMaxDisplayPixels on the
// longest side (aspect-preserving). Display pixmaps larger than this add no
// legibility over the capped base render but cost tens of megabytes.
inline QSize boundDisplaySize(const QSize &size)
{
    if (size.isEmpty()) {
        return size;
    }
    const int longest = size.width() > size.height() ? size.width() : size.height();
    if (longest <= kMaxDisplayPixels) {
        return size;
    }
    return size.scaled(QSize(kMaxDisplayPixels, kMaxDisplayPixels), Qt::KeepAspectRatio);
}

// Display size for a rendered base pixmap inside `available`:
// - FitPage: aspect-preserving fit inside available (up- and down-scale, so
//   small pages still fill the Quick Look-style window).
// - FitWidth: width matches available, height follows aspect (may exceed
//   available height; the scroll area pans). Capped at kMaxDisplayPixels.
// - Manual: base scaled by `manualZoom` (clamped), ignoring available; the
//   scroll area pans when it overflows. Capped at kMaxDisplayPixels.
// Empty inputs return `base` unchanged; null base returns null.
//
// NOTE: the preview window does not use Manual mode directly. Percentages
// are fit-relative (100 % == the fitted size), so the window first fits the
// base into the viewport and then applies zoomedDisplaySize() below. This
// keeps every zoom step relative to what is on screen: leaving a fit mode
// always starts from 100 % of the current view instead of jumping to a
// fraction of the 2048 px base render.
inline QSize fittedDisplaySize(const QSize &base, const QSize &available, FitMode mode,
                               double manualZoom = 1.0)
{
    if (base.isEmpty()) {
        return base;
    }
    if (mode == FitMode::Manual) {
        const double zoom = clampZoom(manualZoom);
        const long long width =
            static_cast<long long>(static_cast<double>(base.width()) * zoom + 0.5);
        const long long height =
            static_cast<long long>(static_cast<double>(base.height()) * zoom + 0.5);
        if (width < 1 || height < 1) {
            return base;
        }
        return boundDisplaySize(QSize(static_cast<int>(width), static_cast<int>(height)));
    }
    if (available.isEmpty()) {
        return base;
    }
    if (mode == FitMode::FitWidth) {
        if (base.width() < 1) {
            return base;
        }
        const double scale =
            static_cast<double>(available.width()) / static_cast<double>(base.width());
        if (!(scale > 0.0)) {
            return base;
        }
        const long long height =
            static_cast<long long>(static_cast<double>(base.height()) * scale + 0.5);
        if (height < 1) {
            return base;
        }
        return boundDisplaySize(QSize(available.width(), static_cast<int>(height)));
    }
    // FitPage.
    return base.scaled(available, Qt::KeepAspectRatio);
}

// Scales an already-fitted size by a fit-relative zoom factor (100 % == the
// fitted size), aspect-preserving, capped at kMaxDisplayPixels. Empty input
// returns empty; non-positive results fall back to the input.
inline QSize zoomedDisplaySize(const QSize &fitted, double zoom)
{
    if (fitted.isEmpty()) {
        return fitted;
    }
    const double factor = clampZoom(zoom);
    const long long width =
        static_cast<long long>(static_cast<double>(fitted.width()) * factor + 0.5);
    const long long height =
        static_cast<long long>(static_cast<double>(fitted.height()) * factor + 0.5);
    if (width < 1 || height < 1) {
        return fitted;
    }
    return boundDisplaySize(QSize(static_cast<int>(width), static_cast<int>(height)));
}

// Short user-facing zoom feedback: "Fit page", "Fit width", or "125%".
inline QString zoomLabel(FitMode mode, double manualZoom = 1.0)
{
    if (mode == FitMode::FitPage) {
        return QStringLiteral("Fit page");
    }
    if (mode == FitMode::FitWidth) {
        return QStringLiteral("Fit width");
    }
    const int percent = static_cast<int>(clampZoom(manualZoom) * 100.0 + 0.5);
    return QStringLiteral("%1%").arg(percent);
}

// "Page X of N" indicator (1-based for display, 0-based internally).
inline QString pageLabel(int currentZeroBased, int total)
{
    if (total < 1) {
        return QStringLiteral("No pages");
    }
    int current = currentZeroBased + 1;
    if (current < 1) {
        current = 1;
    }
    if (current > total) {
        current = total;
    }
    return QStringLiteral("Page %1 of %2").arg(current).arg(total);
}

// Shared status-line text so the window cannot drift between its update
// sites (open, cache hit, async completion, loading indication).
inline QString statusText(int currentZeroBased, int total, const QSize &size)
{
    int current = currentZeroBased + 1;
    if (current < 1) {
        current = 1;
    }
    if (total >= 1 && current > total) {
        current = total;
    }
    return QStringLiteral("PDF document — page %1 of %2 — %3 × %4 px")
        .arg(current)
        .arg(total)
        .arg(size.width())
        .arg(size.height());
}

// Lightweight loading indication shown while a background render is pending.
inline QString loadingText(int currentZeroBased, int total)
{
    int current = currentZeroBased + 1;
    if (current < 1) {
        current = 1;
    }
    if (total >= 1 && current > total) {
        current = total;
    }
    return QStringLiteral("PDF document — page %1 of %2 — loading…").arg(current).arg(total);
}

} // namespace pdf_zoom
