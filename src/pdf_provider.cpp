#include "pdf_provider.h"

#include "pdf_preview.h"

#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <QWidget>

ProviderSupport pdfProviderSupports(const QString &path)
{
    return isPdfFile(path) ? ProviderSupport::Yes : ProviderSupport::No;
}

bool renderPdfPreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
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
    const QPixmap original = QPixmap::fromImage(pdf.image);
    if (original.isNull()) {
        statusLabel->setText(QStringLiteral("Could not load a PDF preview of this file "
                                            "(unreadable or corrupt)."));
        return false;
    }
    const QSize imageSize = original.size();
    statusLabel->setText(QStringLiteral("PDF document — page 1 of %1 — %2 × %3 px")
                             .arg(pdf.pageCount)
                             .arg(imageSize.width())
                             .arg(imageSize.height()));

    // Parent the view to the host widget owning the layout so Qt destroys it
    // with the window; the dispatch layer tracks it via the object name and
    // caches the original pixmap for resize scaling.
    QWidget *host = layout->parentWidget();
    if (host == nullptr) {
        host = qobject_cast<QWidget *>(statusLabel->parent());
    }
    auto *imageLabel = new QLabel(host);
    imageLabel->setObjectName(QStringLiteral("imageLabel"));
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    imageLabel->setMinimumSize(160, 120);
    // The dispatch layer copies this pixmap as the resize source, then scales
    // the label to its current size.
    imageLabel->setPixmap(original);
    layout->addWidget(imageLabel, 1);
    return true;
}

const PreviewProvider &pdfProvider()
{
    static const PreviewProvider provider = {"pdf", &pdfProviderSupports, &renderPdfPreview};
    return provider;
}
