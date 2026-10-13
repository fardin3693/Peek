#pragma once

#include <QString>

// Lightweight audio dispatch. Qt Multimedia only; no new dependencies, and
// independent of PEEK_WITH_PDF. This module never opens media files: it only
// matches known audio extensions so PreviewWindow can route them to the
// streaming QMediaPlayer path. Decode failures stay the player's job and
// surface as a status message, never a crash.

// True for known audio extensions (case-insensitive): mp3, wav, flac, ogg,
// opus, m4a, aac, wma. Unknown extensions are never treated as audio, so
// unrelated files stay on their existing preview paths.
bool isAudioFile(const QString &path);
