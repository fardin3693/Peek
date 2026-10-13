#include "preview_window.h"

#include "preview_request.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

#include "audio_preview.h"
#include "text_preview.h"
#include "video_preview.h"
#ifdef PEEK_WITH_PDF
#include "pdf_preview.h"
#endif
#ifdef PEEK_WITH_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoWidget>
#endif

namespace
{

constexpr int kMaxDecodeDimension = 2048;
constexpr int kMaxWindowWidth = 1024;
constexpr int kMaxWindowHeight = 768;
constexpr int kLabelAllowance = 120;

#ifdef PEEK_WITH_MULTIMEDIA
// Formats milliseconds as m:ss for the playback-position indicator. Shared
// by the video and audio players, which have identical control layouts.
QString formatMediaTime(qint64 ms)
{
    if (ms < 0) {
        ms = 0;
    }
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}
#endif

} // namespace

PreviewWindow::~PreviewWindow()
{
#ifdef PEEK_WITH_MULTIMEDIA
    // Release backend media resources explicitly: stopping unblocks the
    // streaming pipeline and clearing the source drops the demuxer/decoder
    // state before the child widgets are torn down. Video and audio use
    // independent players so either can be released without touching the
    // other.
    if (m_videoPlayer != nullptr) {
        m_videoPlayer->stop();
        m_videoPlayer->setSource(QUrl());
    }
    if (m_audioPlayer != nullptr) {
        m_audioPlayer->stop();
        m_audioPlayer->setSource(QUrl());
    }
#endif
}

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
    bool showingVideo = false;
    bool showingAudio = false;
    if (!request.isDirectory) {
        // Dispatch order: PDF, then text, then audio, then video, then the
        // QImageReader image path, then the generic fallback inside
        // showImageFile. Each handler owns its status message; a claimed file
        // never falls through to an unrelated preview. Audio and video
        // extension sets are disjoint, so their relative order is arbitrary.
        bool handled = false;
#ifdef PEEK_WITH_PDF
        if (!handled && isPdfFile(request.path)) {
            showingImage = showPdfFile(request.path, statusLabel, layout);
            handled = true;
        }
#endif
        if (!handled && isTextFile(request.path)) {
            showingText = showTextFile(request.path, statusLabel, layout);
            handled = true;
        }
#ifdef PEEK_WITH_MULTIMEDIA
        if (!handled && isAudioFile(request.path)) {
            showingAudio = showAudioFile(request.path, statusLabel, layout);
            handled = true;
        }
        if (!handled && isVideoFile(request.path)) {
            showingVideo = showVideoFile(request.path, statusLabel, layout);
            handled = true;
        }
#else
        // Without the Multimedia backend the dispatch still recognizes media
        // extensions so they report as unsupported instead of reaching the
        // image path with a misleading message.
        if (!handled && isAudioFile(request.path)) {
            statusLabel->setText(QStringLiteral("No audio preview is available for this file "
                                                "(Qt Multimedia is not installed)."));
            handled = true;
        }
        if (!handled && isVideoFile(request.path)) {
            statusLabel->setText(QStringLiteral("No video preview is available for this file "
                                                "(Qt Multimedia is not installed)."));
            handled = true;
        }
#endif
        if (!handled) {
            showingImage = showImageFile(request.path, statusLabel, layout);
        }
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
    } else if (showingText || showingVideo || showingAudio) {
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

bool PreviewWindow::isVideoPreview() const
{
#ifdef PEEK_WITH_MULTIMEDIA
    return m_videoPlayer != nullptr;
#else
    return false;
#endif
}

bool PreviewWindow::isAudioPreview() const
{
#ifdef PEEK_WITH_MULTIMEDIA
    return m_audioPlayer != nullptr;
#else
    return false;
#endif
}

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

#ifdef PEEK_WITH_MULTIMEDIA
bool PreviewWindow::showVideoFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    // Streaming playback: QMediaPlayer reads through the FFmpeg backend and
    // never loads the whole file into memory. Playback starts paused; the
    // user opts in with the Play button.
    m_videoPlayer = new QMediaPlayer(this);
    auto *audio = new QAudioOutput(this);
    m_videoPlayer->setAudioOutput(audio);

    auto *videoWidget = new QVideoWidget(this);
    videoWidget->setObjectName(QStringLiteral("videoWidget"));
    videoWidget->setAspectRatioMode(Qt::KeepAspectRatio);
    videoWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoWidget->setMinimumSize(160, 120);
    m_videoPlayer->setVideoOutput(videoWidget);
    m_videoPlayer->setSource(QUrl::fromLocalFile(path));

    m_playButton = new QPushButton(QStringLiteral("Play"), this);
    m_playButton->setObjectName(QStringLiteral("playButton"));
    m_seekSlider = new QSlider(Qt::Horizontal, this);
    m_seekSlider->setObjectName(QStringLiteral("seekSlider"));
    m_seekSlider->setRange(0, 0);
    m_positionLabel =
        new QLabel(formatMediaTime(0) + QStringLiteral(" / ") + formatMediaTime(0), this);
    m_positionLabel->setObjectName(QStringLiteral("positionLabel"));
    m_positionLabel->setTextFormat(Qt::PlainText);

    auto *controls = new QWidget(this);
    controls->setObjectName(QStringLiteral("videoControls"));
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->addWidget(m_playButton);
    controlsLayout->addWidget(m_seekSlider, 1);
    controlsLayout->addWidget(m_positionLabel);

    statusLabel->setText(QStringLiteral("Video preview — paused."));

    connect(m_playButton, &QPushButton::clicked, this, [this] {
        if (m_videoPlayer == nullptr) {
            return;
        }
        if (m_videoPlayer->playbackState() == QMediaPlayer::PlayingState) {
            m_videoPlayer->pause();
        } else {
            m_videoPlayer->play();
        }
    });
    connect(m_videoPlayer, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (m_playButton != nullptr) {
                    m_playButton->setText(state == QMediaPlayer::PlayingState
                                              ? QStringLiteral("Pause")
                                              : QStringLiteral("Play"));
                }
            });
    connect(m_videoPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (m_seekSlider != nullptr) {
            m_seekSlider->setRange(0, static_cast<int>(qMax<qint64>(duration, 0)));
        }
        if (m_positionLabel != nullptr && m_videoPlayer != nullptr) {
            m_positionLabel->setText(formatMediaTime(m_videoPlayer->position()) +
                                     QStringLiteral(" / ") + formatMediaTime(duration));
        }
    });
    connect(m_videoPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (m_videoPlayer == nullptr) {
            return;
        }
        if (m_seekSlider != nullptr && !m_seekSlider->isSliderDown()) {
            m_seekSlider->setValue(static_cast<int>(position));
        }
        if (m_positionLabel != nullptr) {
            m_positionLabel->setText(formatMediaTime(position) + QStringLiteral(" / ") +
                                     formatMediaTime(m_videoPlayer->duration()));
        }
    });
    connect(m_seekSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_videoPlayer != nullptr) {
            m_videoPlayer->setPosition(static_cast<qint64>(value));
        }
    });
    // Unsupported codecs, missing backends, and invalid files report through
    // the video status line. The player widget stays in place so the failure
    // never swaps in an unrelated preview.
    connect(m_videoPlayer, &QMediaPlayer::errorOccurred, this,
            [statusLabel](QMediaPlayer::Error, const QString &errorString) {
                QString detail = errorString.trimmed();
                if (detail.isEmpty()) {
                    detail = QStringLiteral("unreadable or unsupported");
                }
                statusLabel->setText(QStringLiteral("Could not play this video (%1).").arg(detail));
            });

    layout->addWidget(videoWidget, 1);
    layout->addWidget(controls);
    return true;
}

bool PreviewWindow::showAudioFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout)
{
    // Streaming playback like video, but with no video output: QMediaPlayer
    // reads through the FFmpeg backend and never loads the whole file into
    // memory. Playback starts paused; the user opts in with the Play button.
    // The player is independent of the video player so the two backends can
    // never share state across repeated previews.
    m_audioPlayer = new QMediaPlayer(this);
    auto *audio = new QAudioOutput(this);
    m_audioPlayer->setAudioOutput(audio);
    m_audioPlayer->setSource(QUrl::fromLocalFile(path));

    auto *audioLabel = new QLabel(QStringLiteral("Audio preview"), this);
    audioLabel->setObjectName(QStringLiteral("audioLabel"));
    audioLabel->setAlignment(Qt::AlignCenter);
    audioLabel->setTextFormat(Qt::PlainText);
    audioLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    audioLabel->setMinimumSize(160, 120);

    m_audioPlayButton = new QPushButton(QStringLiteral("Play"), this);
    m_audioPlayButton->setObjectName(QStringLiteral("playButton"));
    m_audioSeekSlider = new QSlider(Qt::Horizontal, this);
    m_audioSeekSlider->setObjectName(QStringLiteral("seekSlider"));
    m_audioSeekSlider->setRange(0, 0);
    m_audioPositionLabel =
        new QLabel(formatMediaTime(0) + QStringLiteral(" / ") + formatMediaTime(0), this);
    m_audioPositionLabel->setObjectName(QStringLiteral("positionLabel"));
    m_audioPositionLabel->setTextFormat(Qt::PlainText);

    auto *controls = new QWidget(this);
    controls->setObjectName(QStringLiteral("audioControls"));
    auto *controlsLayout = new QHBoxLayout(controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->addWidget(m_audioPlayButton);
    controlsLayout->addWidget(m_audioSeekSlider, 1);
    controlsLayout->addWidget(m_audioPositionLabel);

    statusLabel->setText(QStringLiteral("Audio preview — paused."));

    connect(m_audioPlayButton, &QPushButton::clicked, this, [this] {
        if (m_audioPlayer == nullptr) {
            return;
        }
        if (m_audioPlayer->playbackState() == QMediaPlayer::PlayingState) {
            m_audioPlayer->pause();
        } else {
            m_audioPlayer->play();
        }
    });
    connect(m_audioPlayer, &QMediaPlayer::playbackStateChanged, this,
            [this](QMediaPlayer::PlaybackState state) {
                if (m_audioPlayButton != nullptr) {
                    m_audioPlayButton->setText(state == QMediaPlayer::PlayingState
                                                   ? QStringLiteral("Pause")
                                                   : QStringLiteral("Play"));
                }
            });
    connect(m_audioPlayer, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        if (m_audioSeekSlider != nullptr) {
            m_audioSeekSlider->setRange(0, static_cast<int>(qMax<qint64>(duration, 0)));
        }
        if (m_audioPositionLabel != nullptr && m_audioPlayer != nullptr) {
            m_audioPositionLabel->setText(formatMediaTime(m_audioPlayer->position()) +
                                          QStringLiteral(" / ") + formatMediaTime(duration));
        }
    });
    connect(m_audioPlayer, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (m_audioPlayer == nullptr) {
            return;
        }
        if (m_audioSeekSlider != nullptr && !m_audioSeekSlider->isSliderDown()) {
            m_audioSeekSlider->setValue(static_cast<int>(position));
        }
        if (m_audioPositionLabel != nullptr) {
            m_audioPositionLabel->setText(formatMediaTime(position) + QStringLiteral(" / ") +
                                          formatMediaTime(m_audioPlayer->duration()));
        }
    });
    connect(m_audioSeekSlider, &QSlider::sliderMoved, this, [this](int value) {
        if (m_audioPlayer != nullptr) {
            m_audioPlayer->setPosition(static_cast<qint64>(value));
        }
    });
    // Unsupported codecs, missing backends, and invalid files report through
    // the audio status line. The controls stay in place so the failure never
    // swaps in an unrelated preview.
    connect(m_audioPlayer, &QMediaPlayer::errorOccurred, this,
            [statusLabel](QMediaPlayer::Error, const QString &errorString) {
                QString detail = errorString.trimmed();
                if (detail.isEmpty()) {
                    detail = QStringLiteral("unreadable or unsupported");
                }
                statusLabel->setText(QStringLiteral("Could not play this audio (%1).").arg(detail));
            });

    layout->addWidget(audioLabel, 1);
    layout->addWidget(controls);
    return true;
}
#endif

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

void PreviewWindow::closeEvent(QCloseEvent *event)
{
#ifdef PEEK_WITH_MULTIMEDIA
    // Stop streaming and release backend resources before the widgets are
    // torn down.
    if (m_videoPlayer != nullptr) {
        m_videoPlayer->stop();
    }
    if (m_audioPlayer != nullptr) {
        m_audioPlayer->stop();
    }
#endif
    QWidget::closeEvent(event);
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
