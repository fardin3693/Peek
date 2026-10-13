#pragma once

#include "preview_provider.h"

// First concrete preview provider: plain text (see src/text_preview.h).
//
// Identification is the existing case-insensitive extension match from
// isTextFile(); rendering is the extracted PreviewWindow::showTextFile body
// with identical messages, object names, and sizing policy. No new
// dependencies, no eager work beyond the bounded load in loadTextPreview().
ProviderSupport textProviderSupports(const QString &path);
bool renderTextPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &textProvider();
