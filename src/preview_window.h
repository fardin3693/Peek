#pragma once

#include <QPixmap>
#include <QSize>
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QVBoxLayout;
#ifdef PEEK_WITH_MULTIMEDIA
class QMediaPlayer;
#endif

struct PreviewRequest;

class PreviewWindow : public QWidget
{
  public:
    explicit PreviewWindow(const PreviewRequest &request, QWidget *parent = nullptr);
    ~PreviewWindow() override;

    // True when an image was decoded and is displayed. Directories and
    // unsupported/unreadable files return false and show a status message.
    bool isImagePreview() const;
    // True when a text file was loaded and is displayed in the text view.
    bool isTextPreview() const;
    // True when a video file claimed the preview area: a streaming player
    // widget with play/pause and seek controls. Playback failures surface as
    // a status message on the same widget, never a crash or a fallback swap.
    bool isVideoPreview() const;
    // True when an audio file claimed the preview area: the same streaming
    // QMediaPlayer backend as video but with no video output — playback
    // controls plus a duration indicator instead of a picture. Always false
    // without PEEK_WITH_MULTIMEDIA, where audio extensions report as
    // unsupported instead.
    bool isAudioPreview() const;
    QSize originalImageSize() const;

    // Aspect-preserving fit of `original` inside `bounds` (Qt::KeepAspectRatio).
    // Returns `original` when either size is empty/null.
    static QSize fittedSize(const QSize &original, const QSize &bounds);

  protected:
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

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
#ifdef PEEK_WITH_MULTIMEDIA
    // Claims the preview area for a video file: streaming QMediaPlayer output
    // plus play/pause and seek controls. Always returns true so unsupported
    // codecs, missing backends, and invalid files report through the video
    // status line instead of falling through to unrelated previews.
    bool showVideoFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
    // Claims the preview area for an audio file: streaming QMediaPlayer with
    // audio output but no video output, plus the same play/pause and seek
    // controls. Same always-true contract as showVideoFile. Kept as a
    // separate small function (rather than a shared player abstraction) so
    // audio and video resources stay independent and cannot interfere.
    bool showAudioFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
#endif

    QLabel *m_imageLabel = nullptr;
    QPlainTextEdit *m_textView = nullptr;
    QPixmap m_original;
#ifdef PEEK_WITH_MULTIMEDIA
    QMediaPlayer *m_videoPlayer = nullptr;
    QPushButton *m_playButton = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_positionLabel = nullptr;
    QMediaPlayer *m_audioPlayer = nullptr;
    QPushButton *m_audioPlayButton = nullptr;
    QSlider *m_audioSeekSlider = nullptr;
    QLabel *m_audioPositionLabel = nullptr;
#endif
};
