#pragma once

#include <QString>

// Lightweight video dispatch. Qt Multimedia only; no new dependencies, and
// independent of PEEK_WITH_PDF. This module never opens media files: it only
// matches known video extensions so PreviewWindow can route them to the
// streaming QMediaPlayer path. Decode failures stay the player's job and
// surface as a status message, never a crash.

// True for known video extensions (case-insensitive): mp4, mkv, webm, mov,
// avi, m4v. Unknown extensions are never treated as video, so unrelated
// files stay on the image/generic fallback path.
bool isVideoFile(const QString &path);
