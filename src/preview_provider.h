#pragma once

#include <QString>

class QLabel;
class QVBoxLayout;

// Minimal preview-provider interface (see docs/provider-contract.md).
//
// A provider answers "is this file mine?" with a cheap, bounded supports()
// check (extension match today; no content decoding), then renders only when
// it claimed the file. Dispatch stays a deterministic, compile-time-ordered
// list: first Yes wins, render failure never re-dispatches, and the claiming
// provider owns its status message.
//
// Ownership: render() parents any widgets it creates to the host widget that
// owns `layout` (via layout->parentWidget(), falling back to the status
// label's parent), so Qt destroys them with the window. Callers track the
// created views (e.g. via findChild on the stable object names) for queries
// such as isTextPreview(). Render never throws, never blocks on eager work,
// and handles null widgets by returning false.
enum class ProviderSupport { No, Yes };

struct PreviewProvider {
    const char *name;                                 // e.g. "text"
    ProviderSupport (*supports)(const QString &path); // cheap, no decoding
    // Renders into the shared layout and sets statusLabel. Returns true when
    // a preview is displayed; on failure sets a status message, creates no
    // view, and returns false without offering the file to another provider.
    bool (*render)(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
};
