#include "image_provider.h"
#include "preview_util.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QSet>
#include <QVBoxLayout>
#include <QWidget>

#include <utility>

namespace
{

const QSet<QString> &supportedImageExtensions()
{
    static const QSet<QString> extensions = [] {
        QSet<QString> names;
        for (const QByteArray &format : QImageReader::supportedImageFormats()) {
            names.insert(QString::fromLatin1(format).toLower());
        }
        return names;
    }();
    return extensions;
}

} // namespace

ProviderSupport imageProviderSupports(const QString &path)
{
    return supportedImageExtensions().contains(QFileInfo(path).suffix().toLower())
               ? ProviderSupport::Yes
               : ProviderSupport::No;
}

bool renderImagePreview(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    if (statusLabel == nullptr || layout == nullptr) {
        return false;
    }
    // Bounded decode shared with Markdown image resources: capped up
    // front when the native size is known, bounded request + post-decode
    // shrink when it is not (see preview_util.h for the residual risk note).
    QString detail;
    QImage image = peek_util::loadBoundedImage(path, &detail);
    if (image.isNull()) {
        statusLabel->setText(QStringLiteral("Could not load an image preview of this "
                                            "file (%1).")
                                 .arg(detail));
        return false;
    }
    const QPixmap original = QPixmap::fromImage(std::move(image));
    if (original.isNull()) {
        statusLabel->setText(QStringLiteral("Could not load an image preview of this "
                                            "file (unreadable or corrupt)."));
        return false;
    }
    const QSize imageSize = original.size();
    QString format = QString::fromLatin1(QImageReader::imageFormat(path)).toUpper();
    if (format.isEmpty()) {
        format = QStringLiteral("IMAGE");
    }
    statusLabel->setText(QStringLiteral("%1 image — %2 × %3 px")
                             .arg(format)
                             .arg(imageSize.width())
                             .arg(imageSize.height()));

    // Parent the view to the host widget owning the layout so Qt destroys it
    // with the window; the dispatch layer tracks it via the object name and
    // caches the original pixmap for resize scaling.
    QWidget *host = peek_util::hostWidget(layout, statusLabel);
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

const PreviewProvider &imageProvider()
{
    static const PreviewProvider provider = {"image", &imageProviderSupports, &renderImagePreview};
    return provider;
}
