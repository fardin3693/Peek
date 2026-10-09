#pragma once

#include <QPixmap>
#include <QSize>
#include <QWidget>

class QLabel;

struct PreviewRequest;

class PreviewWindow : public QWidget
{
  public:
    explicit PreviewWindow(const PreviewRequest &request, QWidget *parent = nullptr);

    // True when an image was decoded and is displayed. Directories and
    // unsupported/unreadable files return false and show a status message.
    bool isImagePreview() const;
    QSize originalImageSize() const;

    // Aspect-preserving fit of `original` inside `bounds` (Qt::KeepAspectRatio).
    // Returns `original` when either size is empty/null.
    static QSize fittedSize(const QSize &original, const QSize &bounds);

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    void updateScaledPixmap();

    QLabel *m_imageLabel = nullptr;
    QPixmap m_original;
};
