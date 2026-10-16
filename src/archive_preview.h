#pragma once

#include <QList>
#include <QString>
#include <QtGlobal>

// Lightweight ZIP listing for archive preview. Qt Core only, no new
// dependencies, and always built (listing needs no decompression backend).
//
// The reader parses only the end-of-central-directory record and the central
// directory at the end of the file. Entry contents (local headers and file
// data) are never read, extracted, executed, or opened, and nothing is ever
// written to the filesystem, so archived paths cannot escape anywhere.
//
// Resource limits (conservative, documented here so providers and tests share
// them; reaching a limit yields an error message or a truncated listing,
// never a crash or an unbounded allocation):
// - kMaxZipEntries: at most this many entries are enumerated; larger archives
//   still succeed with truncated=true ("showing first L of N").
// - kMaxZipCentralDirBytes: the central-directory blob is never read beyond
//   this; larger declarations are rejected before any allocation.
// - kMaxZipEntryNameBytes: a single entry name beyond this rejects the archive
//   before the name is kept.
// - The end-of-central-directory search scans only the last 64 KiB + 22 bytes
//   (the maximum EOCD footprint), never the whole file.
// - Totals accumulate in 64-bit integers with saturation, so hostile declared
//   sizes cannot wrap the summary.
// - ZIP64 and multi-disk archives are recognized and reported as unsupported
//   rather than parsed speculatively.
constexpr int kMaxZipEntries = 10000;
constexpr qint64 kMaxZipCentralDirBytes = 8LL * 1024LL * 1024LL;
constexpr int kMaxZipEntryNameBytes = 4096;

struct ArchiveEntry {
    QString name; // Relative path as stored, kept verbatim for display only.
    bool isDirectory = false;
    quint64 uncompressedSize = 0;
    quint64 compressedSize = 0;
    bool encrypted = false; // Strong enough to list, never decrypted here.
};

struct ArchiveListingResult {
    QList<ArchiveEntry> entries; // Central-directory order, capped at kMaxZipEntries.
    QString error;               // Empty on success (including truncated success).
    bool truncated = false;      // True when the entry cap stopped enumeration.
    quint64 totalUncompressedSize = 0;
    quint64 totalCompressedSize = 0;
    quint64 declaredEntryCount = 0; // EOCD-declared total, for "showing first L of N".
};

// True for known archive extensions (case-insensitive): zip today. Unknown
// extensions are never treated as archives, so unrelated files stay on their
// existing preview paths. Extension-only and cheap: no content is read.
bool isArchiveFile(const QString &path);

// Lists the entries of a ZIP archive without extracting anything. Never
// follows archived paths or writes files. Corrupt, truncated, encrypted-flag,
// ZIP64, multi-disk, and over-limit archives yield either an error string or
// a (possibly truncated) listing — never a crash and never entry contents.
ArchiveListingResult listZipEntries(const QString &path);
