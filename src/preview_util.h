#pragma once

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QSize>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <climits>

// Shared preview helpers (image decode cap, media time, host lookup, slider
// clamp). Qt Core/Gui only; no new dependencies. Documented once here so the
// image/markdown/audio/video paths cannot diverge.
namespace peek_util
{

// Longest side allowed for a decoded image. Applied at decode time via
// QImageReader::setScaledSize when the native size is known, as a bounded
// requested size when it is not (SVG-class formats), and as a post-decode
// shrink so the window never holds a giant pixmap.
constexpr int kMaxDecodeDimension = 2048;

// Shrink-only fit of `original` inside `bounds`. Small images are never
// upscaled (raw QSize::scaled would upscale — do not use it directly).
inline QSize fittedPreviewSize(const QSize &original, const QSize &bounds)
{
    if (original.isEmpty() || bounds.isEmpty()) {
        return original;
    }
    if (original.width() <= bounds.width() && original.height() <= bounds.height()) {
        return original;
    }
    return original.scaled(bounds, Qt::KeepAspectRatio);
}

// Formats milliseconds as m:ss for the playback-position indicator. Shared
// by the video and audio players, which have identical control layouts.
inline QString formatMediaTime(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

// Clamps a qint64 media duration/position into the int range of QSlider
// before casting, so durations past ~24.8 days (2^31 ms) cannot wrap to a
// negative range.
inline int clampMsForSlider(qint64 ms)
{
    return static_cast<int>(qBound<qint64>(0, ms, static_cast<qint64>(INT_MAX)));
}

// Shared "find host widget" lookup: the widget owning the layout, falling
// back to the status label's parent. Providers parent their views to the
// host so Qt destroys them with the window.
inline QWidget *hostWidget(QVBoxLayout *layout, QLabel *statusLabel)
{
    if (layout != nullptr) {
        if (QWidget *host = layout->parentWidget()) {
            return host;
        }
    }
    if (statusLabel != nullptr) {
        return qobject_cast<QWidget *>(statusLabel->parent());
    }
    return nullptr;
}

// Bounded image decode shared by the image provider and Markdown image
// resources. When QImageReader reports a valid native size the decode is
// capped up front with setScaledSize. When it does not (SVG via the svg
// image plugin, certain malformed files), a bounded 2048 px requested size
// is passed instead so the decoder cannot render unbounded, and any image
// that still decodes larger than the cap is shrunk before return.
//
// Residual risk (documented, not assumed away): the post-decode shrink runs
// after allocation, so a crafted file whose size metadata is both absent
// and ignored by its plugin can still allocate up to Qt's internal limit
// (~512 MB) before the shrink. This converts the path from "giant pixmap
// held by the window" to "transient oversized allocation", matching the
// mitigation the PDF (pre-allocation bound) and text (bounded read) paths
// already provide in their own ways.
inline QImage loadBoundedImage(const QString &path, QString *errorDetail = nullptr)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize nativeSize = reader.size();
    const QSize cap(kMaxDecodeDimension, kMaxDecodeDimension);
    if (nativeSize.isValid() && !nativeSize.isEmpty()) {
        const QSize decodeSize = fittedPreviewSize(nativeSize, cap);
        if (decodeSize != nativeSize) {
            reader.setScaledSize(decodeSize);
        }
    } else {
        reader.setScaledSize(cap);
    }
    QImage image = reader.read();
    if (image.isNull()) {
        if (errorDetail != nullptr) {
            QString detail = reader.errorString();
            if (detail.isEmpty()) {
                detail = QStringLiteral("unreadable or corrupt");
            }
            *errorDetail = detail;
        }
        return {};
    }
    if (image.width() > kMaxDecodeDimension || image.height() > kMaxDecodeDimension) {
        image = image.scaled(fittedPreviewSize(image.size(), cap), Qt::KeepAspectRatio,
                             Qt::FastTransformation);
    }
    return image;
}

// Resolves a Markdown image/stylesheet URL to an absolute local path inside
// `baseDir`, or returns empty when the URL must render as a placeholder.
// Relative URLs resolve against the document directory (never the process
// working directory); absolute file: URLs are allowed only when they live
// inside the document directory; remote/unknown schemes are always blocked.
inline QString resolveSiblingResource(const QUrl &url, const QString &baseDir)
{
    if (url.scheme() != QStringLiteral("file") && !url.scheme().isEmpty()) {
        return {};
    }
    QString candidate;
    if (url.isLocalFile()) {
        candidate = url.toLocalFile();
        if (QDir::isRelativePath(candidate)) {
            candidate = QDir(baseDir).filePath(candidate);
        }
    } else {
        // Empty scheme: document-relative reference such as "pic.png" or
        // "sub/pic.png". QUrl::path() carries the percent-decoded path.
        if (url.isRelative()) {
            candidate = QDir(baseDir).filePath(url.path());
        } else {
            return {};
        }
    }
    if (candidate.isEmpty()) {
        return {};
    }
    const QString canonicalBase = QFileInfo(baseDir).canonicalFilePath();
    const QString canonicalCandidate = QFileInfo(candidate).canonicalFilePath();
    if (canonicalBase.isEmpty() || canonicalCandidate.isEmpty()) {
        return {};
    }
    if (canonicalCandidate != canonicalBase &&
        !canonicalCandidate.startsWith(canonicalBase + QLatin1Char('/'))) {
        return {};
    }
    return canonicalCandidate;
}

} // namespace peek_util
