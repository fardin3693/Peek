#include "archive_preview.h"

#include <QFile>
#include <QFileInfo>

namespace
{

constexpr quint32 kCentralHeaderSignature = 0x02014b50u;
constexpr quint32 kEndOfCentralDirSignature = 0x06054b50u;
constexpr int kEndOfCentralDirMinSize = 22;
constexpr quint32 kEndOfCentralDirMaxComment = 65535u;
constexpr quint16 kZip64CountSentinel = 0xFFFFu;
constexpr quint32 kZip64SizeSentinel = 0xFFFFFFFFu;
constexpr quint16 kFlagEncrypted = 0x0001u;
constexpr quint16 kFlagUtf8 = 0x0800u;
constexpr quint32 kDosDirectoryAttribute = 0x00000010u;

quint16 readU16(const uchar *p)
{
    return static_cast<quint16>(p[0] | (static_cast<quint16>(p[1]) << 8u));
}

quint32 readU32(const uchar *p)
{
    return static_cast<quint32>(p[0] | (static_cast<quint32>(p[1]) << 8u) |
                                (static_cast<quint32>(p[2]) << 16u) |
                                (static_cast<quint32>(p[3]) << 24u));
}

// Saturating 64-bit accumulation: hostile declared sizes can inflate the
// summary but can never wrap it.
void addSaturating(quint64 &total, quint64 value)
{
    constexpr quint64 kMax = 0xFFFFFFFFFFFFFFFFull;
    if (value > kMax - total) {
        total = kMax;
    } else {
        total += value;
    }
}

} // namespace

bool isArchiveFile(const QString &path)
{
    return QFileInfo(path).suffix().compare(QStringLiteral("zip"), Qt::CaseInsensitive) == 0;
}

ArchiveListingResult listZipEntries(const QString &path)
{
    ArchiveListingResult result;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("the file is unreadable");
        return result;
    }
    const qint64 fileSize = file.size();
    if (fileSize < kEndOfCentralDirMinSize) {
        result.error = QStringLiteral("no end-of-central-directory record "
                                      "(not a ZIP file or the file is truncated)");
        return result;
    }

    // The EOCD record is 22 bytes plus a comment of at most 65535 bytes, so it
    // always starts inside this tail window no matter how large the file is.
    const qint64 tailSize = qMin<qint64>(fileSize, static_cast<qint64>(kEndOfCentralDirMaxComment) +
                                                       kEndOfCentralDirMinSize);
    if (!file.seek(fileSize - tailSize)) {
        result.error = QStringLiteral("the file is unreadable");
        return result;
    }
    const QByteArray tail = file.read(tailSize);
    if (tail.size() != tailSize) {
        result.error = QStringLiteral("the file is unreadable");
        return result;
    }
    const QByteArray eocdMagic("\x50\x4b\x05\x06", 4);
    const qsizetype eocdOffsetInTail = tail.lastIndexOf(eocdMagic);
    if (eocdOffsetInTail < 0 || eocdOffsetInTail + kEndOfCentralDirMinSize > tail.size()) {
        result.error = QStringLiteral("no end-of-central-directory record "
                                      "(not a ZIP file or the file is truncated)");
        return result;
    }
    const auto *eocd = reinterpret_cast<const uchar *>(tail.constData() + eocdOffsetInTail);
    const quint16 diskNumber = readU16(eocd + 4);
    const quint16 centralDirDisk = readU16(eocd + 6);
    const quint16 entriesOnDisk = readU16(eocd + 8);
    const quint16 totalEntries = readU16(eocd + 10);
    const quint32 centralDirSize = readU32(eocd + 12);
    const quint32 centralDirOffset = readU32(eocd + 16);

    if (diskNumber != 0 || centralDirDisk != 0) {
        result.error = QStringLiteral("multi-disk archives are not supported");
        return result;
    }
    if (entriesOnDisk == kZip64CountSentinel || totalEntries == kZip64CountSentinel ||
        centralDirSize == kZip64SizeSentinel || centralDirOffset == kZip64SizeSentinel) {
        result.error = QStringLiteral("ZIP64 archives are not supported");
        return result;
    }
    result.declaredEntryCount = static_cast<quint64>(totalEntries);

    // Validate the declared central-directory span before allocating anything
    // for it. The subtraction form keeps hostile offset/size pairs from
    // overflowing the bounds check itself.
    const auto dirSize = static_cast<qint64>(centralDirSize);
    if (dirSize > kMaxZipCentralDirBytes) {
        result.error = QStringLiteral("the central directory is too large to preview "
                                      "(8 MiB limit)");
        return result;
    }
    if (static_cast<qint64>(centralDirOffset) > fileSize || dirSize > fileSize ||
        dirSize > fileSize - static_cast<qint64>(centralDirOffset)) {
        result.error = QStringLiteral("the central directory is out of bounds "
                                      "(the file is corrupt or truncated)");
        return result;
    }
    if (totalEntries == 0) {
        return result; // Valid empty archive: no entries, no error.
    }
    if (dirSize == 0) {
        result.error = QStringLiteral("the central directory is corrupt "
                                      "(entries declared but none stored)");
        return result;
    }

    if (!file.seek(static_cast<qint64>(centralDirOffset))) {
        result.error = QStringLiteral("the file is unreadable");
        return result;
    }
    const QByteArray central = file.read(dirSize);
    if (central.size() != dirSize) {
        result.error = QStringLiteral("the central directory is out of bounds "
                                      "(the file is corrupt or truncated)");
        return result;
    }

    qsizetype pos = 0;
    for (quint64 i = 0; i < result.declaredEntryCount; ++i) {
        if (result.entries.size() >= kMaxZipEntries) {
            result.truncated = true;
            break;
        }
        // Fixed 46-byte header plus variable name/extra/comment runs.
        if (pos + 46 > central.size()) {
            result.error = QStringLiteral("the central directory ends early "
                                          "(the file is corrupt or truncated)");
            result.entries.clear();
            return result;
        }
        const auto *header = reinterpret_cast<const uchar *>(central.constData() + pos);
        if (readU32(header) != kCentralHeaderSignature) {
            result.error = QStringLiteral("the central directory is corrupt");
            result.entries.clear();
            return result;
        }
        const quint16 flags = readU16(header + 8);
        const quint32 compressedSize = readU32(header + 20);
        const quint32 uncompressedSize = readU32(header + 24);
        const quint16 nameLength = readU16(header + 28);
        const quint16 extraLength = readU16(header + 30);
        const quint16 commentLength = readU16(header + 32);
        const quint32 externalAttributes = readU32(header + 38);
        if (nameLength > static_cast<quint16>(kMaxZipEntryNameBytes)) {
            result.error = QStringLiteral("an entry name is too large to preview");
            result.entries.clear();
            return result;
        }
        const qsizetype recordEnd = pos + 46 + static_cast<qsizetype>(nameLength) +
                                    static_cast<qsizetype>(extraLength) +
                                    static_cast<qsizetype>(commentLength);
        if (recordEnd > central.size()) {
            result.error = QStringLiteral("the central directory ends early "
                                          "(the file is corrupt or truncated)");
            result.entries.clear();
            return result;
        }

        ArchiveEntry entry;
        const QByteArray rawName(central.constData() + pos + 46, nameLength);
        if ((flags & kFlagUtf8) != 0u) {
            entry.name = QString::fromUtf8(rawName);
        } else {
            // Pre-UTF-8 writers stored CP437 here; Latin-1 keeps the bytes
            // displayable without a codec dependency (documented approximation).
            entry.name = QString::fromLatin1(rawName);
        }
        entry.uncompressedSize = static_cast<quint64>(uncompressedSize);
        entry.compressedSize = static_cast<quint64>(compressedSize);
        entry.encrypted = (flags & kFlagEncrypted) != 0u;
        // Directories are conventionally stored with a trailing slash; the
        // DOS directory attribute is a fallback for writers that omit it.
        entry.isDirectory = entry.name.endsWith(QLatin1Char('/')) ||
                            (entry.uncompressedSize == 0 && entry.compressedSize == 0 &&
                             (externalAttributes & kDosDirectoryAttribute) != 0u);
        result.entries.append(entry);
        addSaturating(result.totalUncompressedSize, entry.uncompressedSize);
        addSaturating(result.totalCompressedSize, entry.compressedSize);
        pos = recordEnd;
    }
    return result;
}
