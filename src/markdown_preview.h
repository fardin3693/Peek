#pragma once

#include <QString>

// Lightweight Markdown dispatch. Qt Widgets only, no new dependencies, and
// always built.
//
// True for Markdown extensions (case-insensitive): md, markdown. Unknown
// extensions are never treated as Markdown, so unrelated files stay on their
// existing preview paths.
bool isMarkdownFile(const QString &path);
