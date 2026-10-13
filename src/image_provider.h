#pragma once

#include "preview_provider.h"

// Second concrete preview provider: raster images (see PreviewWindow's former
// QImageReader path).
//
// Identification is a cheap, bounded, case-insensitive extension match
// against the formats the linked Qt image plugins report via
// QImageReader::supportedImageFormats(). No file content is read or decoded
// during supports(). Rendering keeps the previous behavior exactly:
// QImageReader with auto-transform (EXIF orientation), shrink-only decode
// capped at 2048 px on the longest side, KeepAspectRatio/SmoothTransformation
// display scaling, the same status messages, and the same "imageLabel"
// object name. No new dependencies (Qt Gui via Widgets).
ProviderSupport imageProviderSupports(const QString &path);
bool renderImagePreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &imageProvider();
