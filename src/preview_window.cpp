#include "preview_window.h"

#include "preview_request.h"

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QShortcut>
#include <QVBoxLayout>

#include <utility>

#include "text_preview.h"

#ifdef PEEK_WITH_PDF
#include "pdf_preview.h"
#endif

namespace
{

constexpr int kMaxDecodeDimension = 2048;
constexpr int kMaxWindowWidth = 1024;
constexpr int kMaxWindowHeight = 768;
constexpr int kLabelAllowance = 120;

} // namespace

PreviewWindow::PreviewWindow(const PreviewRequest &request, QWidget *parent) : QWidget(parent)
{
    QString name = QFileInfo(QDir::cleanPath(request.path)).fileName();
    if (name.isEmpty()) {
        name = request.path;
    }
    setWindowTitle(QStringLiteral("Peek — %1").arg(name));

    auto *layout = new QVBoxLayout(this);

    auto *nameLabel = new QLabel(name, this);
    nameLabel->setObjectName(QStringLiteral("fileNameLabel"));
    nameLabel->setTextFormat(Qt::PlainText);
    nameLabel->setWordWrap(true);
    nameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    layout->addWidget(nameLabel);

    auto *pathLabel = new QLabel(request.path, this);
    pathLabel->setObjectName(QStringLiteral("pathLabel"));
    pathLabel->setTextFormat(Qt::PlainText);
    pathLabel->setWordWrap(true);
    pathLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(pathLabel);

    auto *statusLabel = new QLabel(this);
    statusLabel->setObjectName(QStringLiteral("statusLabel"));
    statusLabel->setTextFormat(Qt::PlainText);
    statusLabel->setWordWrap(true);
    statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(statusLabel);

    bool showingImage = false;
    bool showingText = false;
    if (!request.isDirectory) {
#ifdef PEEK_WITH_PDF
        if (isPdfFile(request.path)) {
            showingImage = showPdfFile(request.path, statusLabel, layout);
        } else if (isTextFile(request.path)) {
            showingText = showTextFile(request.path, statusLabel, layout);
        } else {
            showingImage = showImageFile(request.path, statusLabel, layout);
        }
#else
        if (isTextFile(request.path)) {
            showingText = showTextFile(request.path, statusLabel, layout);
        } else {
            showingImage = showImageFile(request.path, statusLabel, layout);
        }
#endif
    } else {
        statusLabel->setText(QStringLiteral("Directory preview not implemented yet."));
    }

    layout->addStretch();

    if (showingImage) {
        const QSize display =
            fittedSize(m_original.size(), QSize(kMaxWindowWidth, kMaxWindowHeight));
        const int width = qBound(480, display.width(), kMaxWindowWidth);
        const int height = qBound(240, display.height() + kLabelAllowance, kMaxWindowHeight);
        resize(width, height);
    } else if (showingText) {
        resize(640, 480);
    } else {
        resize(480, 240);
    }

    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
}

bool PreviewWindow::isImagePreview() const
{
    return m_imageLabel != nullptr && !m_original.isNull();
}

bool PreviewWindow::isTextPreview() const { return m_textView != nullptr; }

bool PreviewWindow::showImageFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        statusLabel->setText(QStringLiteral("No image preview is available for this file."));
        return false;
    }
    const QSize nativeSize = reader.size();
    if (nativeSize.isValid() && !nativeSize.isEmpty()) {
        const QSize decodeSize =
            fittedSize(nativeSize, QSize(kMaxDecodeDimension, kMaxDecodeDimension));
        if (decodeSize != nativeSize) {
            reader.setScaledSize(decodeSize);
        }
    }
    QImage image = reader.read();
    if (image.isNull()) {
        QString detail = reader.errorString();
        if (detail.isEmpty()) {
            detail = QStringLiteral("unreadable or corrupt");
        }
        statusLabel->setText(QStringLiteral("Could not load an image preview of this "
                                            "file (%1).")
                                 .arg(detail));
        return false;
    }
    m_original = QPixmap::fromImage(std::move(image));
    const QSize imageSize = m_original.size();
    QString format = QString::fromLatin1(reader.format()).toUpper();
    if (format.isEmpty()) {
        format = QStringLiteral("IMAGE");
    }
    statusLabel->setText(QStringLiteral("%1 image — %2 × %3 px")
                             .arg(format)
                             .arg(imageSize.width())
                             .arg(imageSize.height()));

    m_imageLabel = new QLabel(this);
    m_imageLabel->setObjectName(QStringLiteral("imageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_imageLabel->setMinimumSize(160, 120);
    layout->addWidget(m_imageLabel, 1);
    updateScaledPixmap();
    return true;
}

#ifdef PEEK_WITH_PDF
bool PreviewWindow::showPdfFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    const PdfFirstPageResult pdf = renderPdfFirstPage(path);
    if (pdf.image.isNull()) {
        QString detail = pdf.error;
        if (detail.isEmpty()) {
            detail = QStringLiteral("unreadable or corrupt");
        }
        statusLabel->setText(
            QStringLiteral("Could not load a PDF preview of this file (%1).").arg(detail));
        return false;
    }
    m_original = QPixmap::fromImage(pdf.image);
    if (m_original.isNull()) {
        statusLabel->setText(QStringLiteral("Could not load a PDF preview of this file "
                                            "(unreadable or corrupt)."));
        return false;
    }
    const QSize imageSize = m_original.size();
    statusLabel->setText(QStringLiteral("PDF document — page 1 of %1 — %2 × %3 px")
                             .arg(pdf.pageCount)
                             .arg(imageSize.width())
                             .arg(imageSize.height()));

    m_imageLabel = new QLabel(this);
    m_imageLabel->setObjectName(QStringLiteral("imageLabel"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_imageLabel->setMinimumSize(160, 120);
    layout->addWidget(m_imageLabel, 1);
    updateScaledPixmap();
    return true;
}
#endif

QSize PreviewWindow::originalImageSize() const { return m_original.size(); }

bool PreviewWindow::showTextFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    const TextPreviewResult text = loadTextPreview(path);
    if (!text.error.isEmpty()) {
        statusLabel->setText(
            QStringLiteral("Could not load a text preview of this file (%1).").arg(text.error));
        return false;
    }
    if (text.text.isEmpty()) {
        statusLabel->setText(QStringLiteral("Text file — empty (%1 bytes)").arg(text.byteCount));
    } else {
        statusLabel->setText(QStringLiteral("Text file — %1 lines — %2 bytes")
                                 .arg(text.lineCount)
                                 .arg(text.byteCount));
    }

    m_textView = new QPlainTextEdit(this);
    m_textView->setObjectName(QStringLiteral("textView"));
    m_textView->setReadOnly(true);
    // Long lines scroll horizontally instead of forcing a wide window.
    m_textView->setLineWrapMode(QPlainTextEdit::NoWrap);
    const QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_textView->setFont(fixedFont);
    m_textView->setTabStopDistance(QFontMetricsF(fixedFont).horizontalAdvance(QLatin1Char(' ')) *
                                   4.0);
    m_textView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_textView->setMinimumSize(160, 120);
    // Plain text only: never interpreted as rich text or HTML.
    m_textView->setPlainText(text.text);
    layout->addWidget(m_textView, 1);
    return true;
}

QSize PreviewWindow::fittedSize(const QSize &original, const QSize &bounds)
{
    if (original.isEmpty() || bounds.isEmpty()) {
        return original;
    }
    if (original.width() <= bounds.width() && original.height() <= bounds.height()) {
        return original;
    }
    return original.scaled(bounds, Qt::KeepAspectRatio);
}

void PreviewWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateScaledPixmap();
}

void PreviewWindow::updateScaledPixmap()
{
    if (m_imageLabel == nullptr || m_original.isNull()) {
        return;
    }
    const QSize available = m_imageLabel->size();
    if (available.isEmpty()) {
        return;
    }
    const QPixmap scaled =
        m_original.scaled(available, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_imageLabel->setPixmap(scaled);
}
