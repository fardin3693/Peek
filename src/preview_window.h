#pragma once

#include <QPixmap>
#include <QSize>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QVBoxLayout;

struct PreviewRequest;

class PreviewWindow : public QWidget
{
  public:
    explicit PreviewWindow(const PreviewRequest &request, QWidget *parent = nullptr);

    // True when an image was decoded and is displayed. Directories and
    // unsupported/unreadable files return false and show a status message.
    bool isImagePreview() const;
    // True when a text file was loaded and is displayed in the text view.
    bool isTextPreview() const;
    QSize originalImageSize() const;

    // Aspect-preserving fit of `original` inside `bounds` (Qt::KeepAspectRatio).
    // Returns `original` when either size is empty/null.
    static QSize fittedSize(const QSize &original, const QSize &bounds);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void updateScaledPixmap();

    // Attempts the legacy QImageReader path. Creates the image label and
    // status text on success. Returns true when an image is displayed.
    bool showImageFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
#ifdef PEEK_WITH_PDF
    // Attempts the Poppler first-page path for PDFs. Same contract.
    bool showPdfFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
#endif
    // Attempts the plain-text path. Creates the text view and status text on
    // success. Returns true when text is displayed.
    bool showTextFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);

    QLabel *m_imageLabel = nullptr;
    QPlainTextEdit *m_textView = nullptr;
    QPixmap m_original;
};
