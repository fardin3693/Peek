#pragma once

#include <QString>

// Lightweight plain-text preview. Qt Core only, no new dependencies, and
// independent of PEEK_WITH_PDF, so this module is always built.

// Maximum file size accepted for text preview (2 MiB). The size is checked
// before reading, and the read itself is bounded, so larger files never
// reach the decoder no matter how the file changes mid-read.
constexpr qint64 kMaxTextFileBytes = 2LL * 1024LL * 1024LL;

struct TextPreviewResult {
    QString text;  // Empty on failure, or for an empty file; see error.
    QString error; // Empty on success.
    qint64 byteCount = 0;
    int lineCount = 0;
};

// True for known plain-text extensions (case-insensitive). Unknown
// extensions are never treated as text, so binary files stay on the
// generic fallback path instead of being decoded speculatively.
bool isTextFile(const QString &path);

// Reads at most kMaxTextFileBytes, strips a UTF-8 BOM, and rejects embedded
// NUL bytes and invalid UTF-8. Empty files succeed with empty text. The file
// is only read, never executed or evaluated.
TextPreviewResult loadTextPreview(const QString &path);
