#pragma once

#include "preview_provider.h"

// Archive preview provider: ZIP listing (see src/archive_preview.h).
//
// Identification is the existing cheap, bounded check from isArchiveFile(): a
// case-insensitive ".zip" extension match. No archive bytes are read merely
// to identify a file. Rendering lists the central directory in archive order
// in a read-only table (name, type, uncompressed and compressed sizes) with a
// summary status line; entry contents are never extracted, executed, or
// opened, and nothing is written to the filesystem. Corrupt, truncated,
// ZIP64, multi-disk, and over-limit archives stay claimed with a status
// message and never fall through to an unrelated preview. No new
// dependencies (Qt Widgets via the existing GUI layer, Qt Core for parsing).
enum ArchiveViewColumn {
    kArchiveNameColumn = 0,
    kArchiveTypeColumn = 1,
    kArchiveUncompressedColumn = 2,
    kArchiveCompressedColumn = 3,
    kArchiveColumnCount = 4,
};

ProviderSupport archiveProviderSupports(const QString &path);
bool renderArchivePreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

// Singleton provider entry used by the dispatch layer. Returned by reference
// so the dispatch list holds a stable address without dynamic allocation.
const PreviewProvider &archiveProvider();
