#include "text_preview.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <cstdint>

namespace
{

// Strict UTF-8 validation. QString::fromUtf8() substitutes U+FFFD for bad
// sequences instead of failing, so validity must be checked separately to
// avoid silently corrupting the preview. Rejects overlong encodings,
// surrogates, and code points above U+10FFFF.
bool isValidUtf8(const QByteArray &bytes)
{
    const auto *data = reinterpret_cast<const unsigned char *>(bytes.constData());
    const qsizetype size = bytes.size();
    qsizetype i = 0;
    while (i < size) {
        const unsigned char first = data[i];
        if (first <= 0x7Fu) {
            ++i;
            continue;
        }
        qsizetype need = 0;
        char32_t codepoint = 0;
        char32_t minimum = 0;
        if (first >= 0xC2u && first <= 0xDFu) {
            need = 1;
            codepoint = static_cast<char32_t>(first & 0x1Fu);
            minimum = 0x80u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            need = 2;
            codepoint = static_cast<char32_t>(first & 0x0Fu);
            minimum = 0x800u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            need = 3;
            codepoint = static_cast<char32_t>(first & 0x07u);
            minimum = 0x10000u;
        } else {
            return false;
        }
        if (i + need >= size) {
            return false;
        }
        for (qsizetype k = 1; k <= need; ++k) {
            const unsigned char cont = data[i + k];
            if (cont < 0x80u || cont > 0xBFu) {
                return false;
            }
            codepoint = (codepoint << 6u) | static_cast<char32_t>(cont & 0x3Fu);
        }
        if (codepoint < minimum || codepoint > 0x10FFFFu ||
            (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            return false;
        }
        i += 1 + need;
    }
    return true;
}

int countLines(const QString &text)
{
    if (text.isEmpty()) {
        return 0;
    }
    int lines = text.count(QLatin1Char('\n'));
    if (!text.endsWith(QLatin1Char('\n'))) {
        ++lines;
    }
    return lines;
}

} // namespace

bool isTextFile(const QString &path)
{
    static const QSet<QString> extensions = {
        QStringLiteral("txt"),  QStringLiteral("md"),    QStringLiteral("markdown"),
        QStringLiteral("json"), QStringLiteral("jsonl"), QStringLiteral("yaml"),
        QStringLiteral("yml"),  QStringLiteral("toml"),  QStringLiteral("csv"),
        QStringLiteral("tsv"),  QStringLiteral("log"),   QStringLiteral("xml"),
        QStringLiteral("html"), QStringLiteral("css"),   QStringLiteral("c"),
        QStringLiteral("h"),    QStringLiteral("cpp"),   QStringLiteral("hpp"),
        QStringLiteral("py"),   QStringLiteral("sh"),    QStringLiteral("lua"),
        QStringLiteral("js"),   QStringLiteral("ts"),    QStringLiteral("rs"),
    };
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

TextPreviewResult loadTextPreview(const QString &path)
{
    TextPreviewResult result;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = QStringLiteral("the file cannot be accessed");
        return result;
    }
    if (info.size() < 0 || info.size() > kMaxTextFileBytes) {
        result.error = QStringLiteral("the file is too large to preview (2 MiB limit)");
        return result;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("the file is unreadable");
        return result;
    }
    // Bound the read itself: the file may grow between the size check and
    // the read, so never pull more than the limit plus one byte.
    const QByteArray raw = file.read(kMaxTextFileBytes + 1);
    if (raw.size() > kMaxTextFileBytes) {
        result.error = QStringLiteral("the file is too large to preview (2 MiB limit)");
        return result;
    }
    result.byteCount = raw.size();
    if (raw.isEmpty()) {
        return result;
    }
    if (raw.contains('\0')) {
        result.error = QStringLiteral("binary content is not supported");
        return result;
    }
    QByteArray bytes = raw;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
    }
    if (!isValidUtf8(bytes)) {
        result.error = QStringLiteral("only UTF-8 text is supported");
        return result;
    }
    result.text = QString::fromUtf8(bytes);
    result.lineCount = countLines(result.text);
    return result;
}
