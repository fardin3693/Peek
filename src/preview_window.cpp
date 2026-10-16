#include "preview_window.h"

#include "preview_request.h"
#include "preview_util.h"

#include <QCloseEvent>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QSlider>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "archive_provider.h"
#include "audio_preview.h"
#include "image_provider.h"
#include "markdown_provider.h"
#include "text_provider.h"
#include "video_preview.h"
#ifdef PEEK_WITH_PDF
#include "pdf_provider.h"
#endif
#ifdef PEEK_WITH_MULTIMEDIA
#include "audio_provider.h"
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoWidget>
#endif

namespace
{

constexpr int kMaxWindowWidth = 1024;
constexpr int kMaxWindowHeight = 768;
constexpr int kLabelAllowance = 120;
constexpr int kSmoothRescaleDelayMs = 100;

#ifdef PEEK_WITH_MULTIMEDIA
using peek_util::formatMediaTime;
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
    bool showingMarkdown = false;
    bool showingVideo = false;
    bool showingAudio = false;
    bool showingArchive = false;
    if (!request.isDirectory) {
        // Dispatch order: PDF, then Markdown, then text, then audio, then
        // video, then archive, then the image provider, then the generic
        // fallback. Each handler owns its status message; a claimed file
        // never falls through to an unrelated preview. Audio and video
        // extension sets are disjoint, so their relative order is arbitrary.
        bool handled = false;
#ifdef PEEK_WITH_PDF
        if (!handled && pdfProvider().supports(request.path) == ProviderSupport::Yes) {
            // PDF is the first provider: render() builds the view and owns
            // the status message. A PDF claim ends dispatch even when the
            // first page cannot be rendered, so corrupt PDFs never fall
            // through to an unrelated preview.
            showingImage = pdfProvider().render(request.path, statusLabel, layout);
            if (showingImage) {
                m_imageLabel = findChild<QLabel *>(QStringLiteral("imageLabel"));
                if (m_imageLabel != nullptr) {
                    m_original = m_imageLabel->pixmap();
                    updateScaledPixmap();
                }
            }
            handled = true;
        }
#endif
        if (!handled && markdownProvider().supports(request.path) == ProviderSupport::Yes) {
            // Markdown precedes text: render() builds the formatted view and
            // owns the status message. A Markdown claim ends dispatch even
            // when the file cannot be decoded, so corrupt Markdown never
            // falls through to an unrelated preview.
            showingMarkdown = markdownProvider().render(request.path, statusLabel, layout);
            if (showingMarkdown) {
                m_markdownView = findChild<QTextBrowser *>(QStringLiteral("markdownView"));
            }
            handled = true;
        }
        if (!handled && textProvider().supports(request.path) == ProviderSupport::Yes) {
            // Text follows Markdown: render() builds the view and owns
            // the status message. A text claim ends dispatch even when the
            // file cannot be decoded, so corrupt text never falls through to
            // an unrelated preview.
            showingText = textProvider().render(request.path, statusLabel, layout);
            if (showingText) {
                m_textView = findChild<QPlainTextEdit *>(QStringLiteral("textView"));
            }
            handled = true;
        }
#ifdef PEEK_WITH_MULTIMEDIA
        if (!handled && audioProvider().supports(request.path) == ProviderSupport::Yes) {
            // Audio is the third provider: render() builds the streaming
            // player plus controls and owns the status message. An audio
            // claim ends dispatch even when the file cannot be played, so
            // invalid audio never falls through to an unrelated preview.
            showingAudio = audioProvider().render(request.path, statusLabel, layout);
            if (showingAudio) {
                m_audioPlayer = findChild<QMediaPlayer *>(QStringLiteral("audioPlayer"));
                m_audioPlayButton = findChild<QPushButton *>(QStringLiteral("playButton"));
                m_audioSeekSlider = findChild<QSlider *>(QStringLiteral("seekSlider"));
                m_audioPositionLabel = findChild<QLabel *>(QStringLiteral("positionLabel"));
            }
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
        if (!handled && archiveProvider().supports(request.path) == ProviderSupport::Yes) {
            // Archive precedes image: render() builds the read-only entry
            // listing and owns the status message. An archive claim ends
            // dispatch even when the file is corrupt or unsupported, so bad
            // archives never fall through to an unrelated preview.
            showingArchive = archiveProvider().render(request.path, statusLabel, layout);
            if (showingArchive) {
                m_archiveView = findChild<QTableWidget *>(QStringLiteral("archiveView"));
            }
            handled = true;
        }
        if (!handled && imageProvider().supports(request.path) == ProviderSupport::Yes) {
            // Image is the last provider: render() builds the view and owns
            // the status message. An image claim ends dispatch even when the
            // file cannot be decoded, so corrupt images never fall through to
            // an unrelated preview.
            showingImage = imageProvider().render(request.path, statusLabel, layout);
            if (showingImage) {
                m_imageLabel = findChild<QLabel *>(QStringLiteral("imageLabel"));
                if (m_imageLabel != nullptr) {
                    m_original = m_imageLabel->pixmap();
                    updateScaledPixmap();
                }
            }
            handled = true;
        }
        if (!handled) {
            statusLabel->setText(QStringLiteral("No image preview is available for this file."));
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
    } else if (showingText || showingMarkdown || showingVideo || showingAudio || showingArchive) {
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

bool PreviewWindow::isMarkdownPreview() const { return m_markdownView != nullptr; }

bool PreviewWindow::isArchivePreview() const { return m_archiveView != nullptr; }

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

QSize PreviewWindow::originalImageSize() const { return m_original.size(); }

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
            m_seekSlider->setRange(0, peek_util::clampMsForSlider(duration));
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
            m_seekSlider->setValue(peek_util::clampMsForSlider(position));
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
    if (m_imageLabel == nullptr || m_original.isNull()) {
        return;
    }
    // Cheap immediate feedback during the drag, then one debounced smooth
    // pass once the size settles.
    updateScaledPixmap(Qt::FastTransformation);
    scheduleSmoothRescale();
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

void PreviewWindow::updateScaledPixmap(Qt::TransformationMode mode)
{
    if (m_imageLabel == nullptr || m_original.isNull()) {
        return;
    }
    const QSize available = m_imageLabel->size();
    if (available.isEmpty()) {
        return;
    }
    if (mode == Qt::SmoothTransformation && available == m_lastSmoothAvailable) {
        return;
    }
    const QPixmap scaled = m_original.scaled(available, Qt::KeepAspectRatio, mode);
    m_imageLabel->setPixmap(scaled);
    if (mode == Qt::SmoothTransformation) {
        m_lastSmoothAvailable = available;
    }
}

void PreviewWindow::scheduleSmoothRescale()
{
    if (m_smoothTimer == nullptr) {
        m_smoothTimer = new QTimer(this);
        m_smoothTimer->setSingleShot(true);
        m_smoothTimer->setInterval(kSmoothRescaleDelayMs);
        connect(m_smoothTimer, &QTimer::timeout, this,
                [this] { updateScaledPixmap(Qt::SmoothTransformation); });
    }
    m_smoothTimer->start(kSmoothRescaleDelayMs);
}
