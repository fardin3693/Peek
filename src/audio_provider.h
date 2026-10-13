#pragma once

#include "preview_provider.h"

// Fourth concrete preview provider: audio playback (see src/audio_preview.h).
//
// Identification is the existing cheap, bounded check from isAudioFile(): a
// case-insensitive extension match (mp3, wav, flac, ogg, opus, m4a, aac,
// wma). No media player is opened and no audio is decoded merely to identify
// a file. Rendering is the extracted PreviewWindow::showAudioFile body with
// identical messages, object names, and sizing policy: a streaming
// QMediaPlayer with audio output but no video output, plus the same
// play/pause and seek controls as video. Playback starts paused; failures
// surface through the audio status line and never re-dispatch.
//
// This header and its implementation are only built into targets that
// define PEEK_WITH_MULTIMEDIA; see CMakeLists.txt. Without the backend the
// dispatch layer still recognizes audio extensions via isAudioFile() and
// reports them as unsupported instead of reaching the image path.
ProviderSupport audioProviderSupports(const QString &path);
bool renderAudioPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &audioProvider();
