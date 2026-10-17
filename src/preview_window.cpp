#include "preview_window.h"

#include "preview_request.h"
#include "preview_util.h"

#include <QCloseEvent>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
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
#include "pdf_preview.h"
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
#ifdef PEEK_WITH_PDF
    stopPdfThread();
#endif
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
            // PDF is the first provider: render() builds the first-page view,
            // owns the status message, and hands the page count back, so the
            // navigation controls know the total without reopening the
            // document. A PDF claim ends dispatch even when the first page
            // cannot be rendered, so corrupt PDFs never fall through to an
            // unrelated preview.
            int pdfPages = 0;
            showingImage = ::renderPdfPreview(request.path, statusLabel, layout, &pdfPages);
            if (showingImage) {
                m_imageLabel = findChild<QLabel *>(QStringLiteral("imageLabel"));
                if (m_imageLabel != nullptr) {
                    m_original = m_imageLabel->pixmap();
                    updateScaledPixmap();
                    // Interactive navigation/zoom wraps the synchronously
                    // rendered first page: the window already shows useful
                    // content while later pages render on demand in the
                    // background.
                    setupPdfInteraction(request.path, pdfPages, statusLabel, layout);
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

#ifdef PEEK_WITH_PDF
bool PreviewWindow::isPdfPreview() const { return m_isPdfPreview && m_imageLabel != nullptr; }

int PreviewWindow::pdfCurrentPage() const { return m_pdfCurrentPage; }

int PreviewWindow::pdfPageCount() const { return m_pdfPageCount; }

double PreviewWindow::pdfZoomFactor() const { return m_pdfZoom; }

pdf_zoom::FitMode PreviewWindow::pdfFitMode() const { return m_pdfFitMode; }
#endif

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
#ifdef PEEK_WITH_PDF
    if (m_isPdfPreview) {
        // The viewport event filter owns PDF refits: it observes the final
        // viewport geometry, which lags the window resize while layout runs.
        // Refitting here as well would scale from a stale viewport.
        return;
    }
#endif
    // Cheap immediate feedback during the drag, then one debounced smooth
    // pass once the size settles.
    updateScaledPixmap(Qt::FastTransformation);
    scheduleSmoothRescale();
}

#ifdef PEEK_WITH_PDF
bool PreviewWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Refit when the scroll viewport settles on its laid-out size. This is
    // what makes the freshly opened preview correct: at construction time the
    // viewport still has Qt's un-laid-out default size, so the setup-time fit
    // is necessarily wrong until the first viewport resize arrives.
    if (m_isPdfPreview && m_pdfScrollArea != nullptr && watched == m_pdfScrollArea->viewport() &&
        event->type() == QEvent::Resize) {
        updatePdfDisplay(Qt::FastTransformation);
        schedulePdfSmoothRescale();
    }
    return QWidget::eventFilter(watched, event);
}
#endif

void PreviewWindow::closeEvent(QCloseEvent *event)
{
#ifdef PEEK_WITH_PDF
    // Invalidate in-flight renders so late worker results are discarded
    // instead of touching a closing window.
    ++m_pdfTicket;
#endif
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

#ifdef PEEK_WITH_PDF
void PdfPageWorker::render(const QString &path, int page, quint64 ticket)
{
    // Own document instance per task: no shared Poppler state across threads.
    // Superseded tasks return before rasterizing: the window bumped the
    // generation ticket past them, so rendering would only waste a full
    // raster whose result is discarded on arrival anyway.
    if (latestTicket != nullptr && ticket != latestTicket->load()) {
        return;
    }
    const PdfPageResult result = renderPdfPage(path, page);
    emit done(ticket, page, result.image, result.error);
}

void PreviewWindow::setupPdfInteraction(const QString &path, int pageCount, QLabel *statusLabel,
                                        QVBoxLayout *layout)
{
    m_isPdfPreview = true;
    m_pdfPath = path;
    m_pdfPageCount = pageCount;
    m_pdfCurrentPage = 0;
    m_pdfZoom = 1.0;
    m_pdfFitMode = pdf_zoom::FitMode::FitPage;
    m_pdfTicket = 0;
    m_pdfStatusLabel = statusLabel;
    insertPdfCache(0, m_original.toImage());

    // Compact controls above the document: navigation on the left, zoom on
    // the right, keeping the document the primary focus.
    m_pdfControls = new QWidget(this);
    m_pdfControls->setObjectName(QStringLiteral("pdfControls"));
    auto *controlsLayout = new QHBoxLayout(m_pdfControls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);

    m_pdfPrevButton = new QPushButton(QStringLiteral("◀"), m_pdfControls);
    m_pdfPrevButton->setObjectName(QStringLiteral("pdfPrevButton"));
    m_pdfPrevButton->setToolTip(QStringLiteral("Previous page (Left, Page Up)"));
    m_pdfPrevButton->setFixedWidth(36);
    m_pdfNextButton = new QPushButton(QStringLiteral("▶"), m_pdfControls);
    m_pdfNextButton->setObjectName(QStringLiteral("pdfNextButton"));
    m_pdfNextButton->setToolTip(QStringLiteral("Next page (Right, Page Down)"));
    m_pdfNextButton->setFixedWidth(36);
    m_pdfPageLabel = new QLabel(m_pdfControls);
    m_pdfPageLabel->setObjectName(QStringLiteral("pdfPageLabel"));
    m_pdfPageLabel->setTextFormat(Qt::PlainText);

    m_pdfZoomOutButton = new QPushButton(QStringLiteral("−"), m_pdfControls);
    m_pdfZoomOutButton->setObjectName(QStringLiteral("pdfZoomOutButton"));
    m_pdfZoomOutButton->setToolTip(QStringLiteral("Zoom out (-)"));
    m_pdfZoomOutButton->setFixedWidth(36);
    m_pdfZoomInButton = new QPushButton(QStringLiteral("+"), m_pdfControls);
    m_pdfZoomInButton->setObjectName(QStringLiteral("pdfZoomInButton"));
    m_pdfZoomInButton->setToolTip(QStringLiteral("Zoom in (+)"));
    m_pdfZoomInButton->setFixedWidth(36);
    m_pdfZoomLabel = new QLabel(m_pdfControls);
    m_pdfZoomLabel->setObjectName(QStringLiteral("pdfZoomLabel"));
    m_pdfZoomLabel->setTextFormat(Qt::PlainText);
    m_pdfZoomLabel->setMinimumWidth(64);
    m_pdfZoomLabel->setAlignment(Qt::AlignCenter);
    m_pdfFitPageButton = new QPushButton(QStringLiteral("Fit page"), m_pdfControls);
    m_pdfFitPageButton->setObjectName(QStringLiteral("pdfFitPageButton"));
    m_pdfFitPageButton->setToolTip(QStringLiteral("Fit page to the preview area (0)"));
    m_pdfFitWidthButton = new QPushButton(QStringLiteral("Fit width"), m_pdfControls);
    m_pdfFitWidthButton->setObjectName(QStringLiteral("pdfFitWidthButton"));
    m_pdfFitWidthButton->setToolTip(QStringLiteral("Fit page width to the preview area"));

    controlsLayout->addWidget(m_pdfPrevButton);
    controlsLayout->addWidget(m_pdfNextButton);
    controlsLayout->addWidget(m_pdfPageLabel);
    controlsLayout->addStretch(1);
    controlsLayout->addWidget(m_pdfZoomOutButton);
    controlsLayout->addWidget(m_pdfZoomInButton);
    controlsLayout->addWidget(m_pdfZoomLabel);
    controlsLayout->addWidget(m_pdfFitPageButton);
    controlsLayout->addWidget(m_pdfFitWidthButton);

    connect(m_pdfPrevButton, &QPushButton::clicked, this, &PreviewWindow::showPreviousPdfPage);
    connect(m_pdfNextButton, &QPushButton::clicked, this, &PreviewWindow::showNextPdfPage);
    connect(m_pdfZoomInButton, &QPushButton::clicked, this, &PreviewWindow::zoomPdfIn);
    connect(m_pdfZoomOutButton, &QPushButton::clicked, this, &PreviewWindow::zoomPdfOut);
    connect(m_pdfFitPageButton, &QPushButton::clicked, this, &PreviewWindow::fitPdfPage);
    connect(m_pdfFitWidthButton, &QPushButton::clicked, this, &PreviewWindow::fitPdfWidth);

    // Move the provider's image view into a scroll area so manual zooms and
    // width fits larger than the viewport can pan. The object name stays
    // "imageLabel" for the existing preview-surface contract.
    int imageIndex = layout->indexOf(m_imageLabel);
    if (imageIndex < 0) {
        imageIndex = layout->count();
    } else {
        layout->removeWidget(m_imageLabel);
    }
    layout->insertWidget(imageIndex, m_pdfControls);
    m_pdfScrollArea = new QScrollArea(this);
    m_pdfScrollArea->setObjectName(QStringLiteral("pdfScrollArea"));
    m_pdfScrollArea->setWidgetResizable(false);
    m_pdfScrollArea->setAlignment(Qt::AlignCenter);
    // Arrow keys drive page navigation by design (Quick Look feel);
    // panning stays on scrollbars/mouse so shortcuts never fight the view.
    m_pdfScrollArea->setFocusPolicy(Qt::NoFocus);
    m_imageLabel->setParent(m_pdfScrollArea->viewport());
    m_pdfScrollArea->setWidget(m_imageLabel);
    layout->insertWidget(imageIndex + 1, m_pdfScrollArea, 1);

    // Keyboard navigation: only Left/Right/PageUp/PageDown for pages and
    // +/-/0 for zoom. Peek otherwise binds just Escape, and Hyprland
    // reserves Super chords, so these plain keys cannot conflict with an
    // existing application or window shortcut.
    auto addKey = [this](QKeySequence key, void (PreviewWindow::*slot)()) {
        auto *shortcut = new QShortcut(key, this);
        shortcut->setContext(Qt::WindowShortcut);
        connect(shortcut, &QShortcut::activated, this, slot);
    };
    addKey(QKeySequence(Qt::Key_Left), &PreviewWindow::showPreviousPdfPage);
    addKey(QKeySequence(Qt::Key_Right), &PreviewWindow::showNextPdfPage);
    addKey(QKeySequence(Qt::Key_PageUp), &PreviewWindow::showPreviousPdfPage);
    addKey(QKeySequence(Qt::Key_PageDown), &PreviewWindow::showNextPdfPage);
    addKey(QKeySequence(Qt::Key_Plus), &PreviewWindow::zoomPdfIn);
    addKey(QKeySequence(Qt::Key_Equal), &PreviewWindow::zoomPdfIn);
    addKey(QKeySequence(Qt::Key_Minus), &PreviewWindow::zoomPdfOut);
    addKey(QKeySequence(Qt::Key_0), &PreviewWindow::fitPdfPage);

    // The worker thread starts lazily on the first navigation (see
    // requestPdfPage()): a preview that is never turned past page one costs
    // no thread on the resource-constrained target.
    updatePdfScrollPolicies();

    // The setup-time fit below necessarily uses the pre-layout viewport, so
    // it is refined by the viewport-resize filter once layout runs.
    m_pdfScrollArea->viewport()->installEventFilter(this);

    updatePdfControls();
    updatePdfDisplay();
    setPdfStatus(pdf_zoom::statusText(0, m_pdfPageCount, m_original.size()));
}

void PreviewWindow::updatePdfControls()
{
    if (!m_isPdfPreview) {
        return;
    }
    if (m_pdfPageLabel != nullptr) {
        m_pdfPageLabel->setText(pdf_zoom::pageLabel(m_pdfCurrentPage, m_pdfPageCount));
    }
    if (m_pdfZoomLabel != nullptr) {
        m_pdfZoomLabel->setText(pdf_zoom::zoomLabel(m_pdfFitMode, m_pdfZoom));
    }
    if (m_pdfPrevButton != nullptr) {
        m_pdfPrevButton->setEnabled(m_pdfCurrentPage > 0);
    }
    if (m_pdfNextButton != nullptr) {
        m_pdfNextButton->setEnabled(m_pdfCurrentPage + 1 < m_pdfPageCount);
    }
}

QSize PreviewWindow::pdfDisplayTarget() const
{
    if (!m_isPdfPreview || m_original.isNull() || m_pdfScrollArea == nullptr) {
        return {};
    }
    const QSize available = m_pdfScrollArea->viewport()->size();
    if (available.isEmpty()) {
        return {};
    }
    if (m_pdfFitMode == pdf_zoom::FitMode::Manual) {
        // Fit-relative zoom: 100 % is the size that fits the viewport, so
        // leaving a fit mode always starts from what is on screen.
        const QSize fit =
            pdf_zoom::fittedDisplaySize(m_original.size(), available, pdf_zoom::FitMode::FitPage);
        return pdf_zoom::zoomedDisplaySize(fit, m_pdfZoom);
    }
    return pdf_zoom::fittedDisplaySize(m_original.size(), available, m_pdfFitMode, m_pdfZoom);
}

void PreviewWindow::updatePdfDisplay(Qt::TransformationMode mode)
{
    if (!m_isPdfPreview || m_imageLabel == nullptr || m_original.isNull() ||
        m_pdfScrollArea == nullptr) {
        return;
    }
    // The viewport is the single source of truth for fitting. Never fall back
    // to the label size here: before layout the label still carries the
    // provider's minimum size, which is exactly how the tiny-sliver first
    // view shipped. An empty (pre-layout) viewport simply waits for the
    // resize filter.
    const QSize target = pdfDisplayTarget();
    if (target.isEmpty()) {
        return;
    }
    const QPixmap scaled = m_original.scaled(target, Qt::IgnoreAspectRatio, mode);
    m_imageLabel->setPixmap(scaled);
    m_imageLabel->resize(scaled.size());
}

void PreviewWindow::requestPdfPage(int page)
{
    if (!m_isPdfPreview) {
        return;
    }
    const auto cached = m_pdfCache.find(page);
    if (cached != m_pdfCache.end() && !cached->isNull()) {
        m_original = QPixmap::fromImage(*cached);
        if (!m_original.isNull()) {
            updatePdfControls();
            updatePdfDisplay();
            setPdfStatus(pdf_zoom::statusText(m_pdfCurrentPage, m_pdfPageCount, m_original.size()));
            return;
        }
    }
    startPdfThread();
    if (m_pdfThread == nullptr) {
        return;
    }
    ++m_pdfTicket;
    setPdfStatus(pdf_zoom::loadingText(m_pdfCurrentPage, m_pdfPageCount));
    emit pdfRenderRequested(m_pdfPath, page, m_pdfTicket.load());
}

void PreviewWindow::insertPdfCache(int page, const QImage &image)
{
    if (image.isNull()) {
        return;
    }
    m_pdfCache.insert(page, image);
    while (m_pdfCache.size() > pdf_zoom::kMaxCachedPages) {
        // Evict the cached page farthest from the current one: bounded,
        // recent-locality cache suitable for low-memory machines.
        int farthest = -1;
        int farthestDistance = -1;
        for (auto it = m_pdfCache.constBegin(); it != m_pdfCache.constEnd(); ++it) {
            const int distance = qAbs(it.key() - m_pdfCurrentPage);
            if (distance > farthestDistance) {
                farthestDistance = distance;
                farthest = it.key();
            }
        }
        if (farthest < 0) {
            break;
        }
        m_pdfCache.remove(farthest);
    }
}

void PreviewWindow::stopPdfThread()
{
    if (m_pdfThread == nullptr) {
        return;
    }
    // Invalidate pending results first so a late worker reply cannot
    // touch half-torn-down widgets, then sever delivery: queued done()
    // signals must not reach a window under destruction.
    ++m_pdfTicket;
    if (m_pdfWorker != nullptr) {
        disconnect(m_pdfWorker, nullptr, this, nullptr);
    }
    m_pdfThread->quit();
    m_pdfThread->wait();
    // Explicit teardown with the worker thread provably stopped, then direct
    // deletion: the sole ownership path. (A finished->deleteLater link was
    // removed because the deferred delete can be delivered on the expiring
    // thread and race this delete — heap-use-after-free in teardown, caught
    // by ASan.) ~QObject purges the worker's undelivered queued calls on
    // destruction.
    delete m_pdfWorker;
    m_pdfWorker = nullptr;
    // The thread object is parented to the window; deleting it directly
    // removes it from the children list safely.
    delete m_pdfThread;
    m_pdfThread = nullptr;
}

void PreviewWindow::startPdfThread()
{
    if (m_pdfThread != nullptr) {
        return;
    }
    m_pdfThread = new QThread(this);
    m_pdfWorker = new PdfPageWorker();
    m_pdfWorker->latestTicket = &m_pdfTicket;
    m_pdfWorker->moveToThread(m_pdfThread);
    // No finished->deleteLater link by design: it can be delivered on the
    // expiring thread and then race the direct delete below (use-after-free
    // in teardown). Single ownership stays in stopPdfThread(): quit, wait for
    // the loop to stop, then delete directly. Undelivered queued calls are
    // purged by ~QObject on destruction.
    connect(this, &PreviewWindow::pdfRenderRequested, m_pdfWorker, &PdfPageWorker::render,
            Qt::QueuedConnection);
    connect(m_pdfWorker, &PdfPageWorker::done, this, &PreviewWindow::onPdfPageRendered,
            Qt::QueuedConnection);
    m_pdfThread->start();
}

void PreviewWindow::schedulePdfSmoothRescale()
{
    if (m_pdfSmoothTimer == nullptr) {
        m_pdfSmoothTimer = new QTimer(this);
        m_pdfSmoothTimer->setSingleShot(true);
        m_pdfSmoothTimer->setInterval(kSmoothRescaleDelayMs);
        connect(m_pdfSmoothTimer, &QTimer::timeout, this,
                [this] { updatePdfDisplay(Qt::SmoothTransformation); });
    }
    m_pdfSmoothTimer->start(kSmoothRescaleDelayMs);
}

void PreviewWindow::updatePdfScrollPolicies()
{
    if (m_pdfScrollArea == nullptr) {
        return;
    }
    // In FitWidth the label is exactly the viewport width, so a demand-driven
    // vertical scrollbar would shrink the viewport after the fit and leave a
    // permanent horizontal scrollbar. Pinning it keeps the fitted width
    // stable (it renders disabled when the content is short; harmless).
    m_pdfScrollArea->setVerticalScrollBarPolicy(m_pdfFitMode == pdf_zoom::FitMode::FitWidth
                                                    ? Qt::ScrollBarAlwaysOn
                                                    : Qt::ScrollBarAsNeeded);
    m_pdfScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
}

void PreviewWindow::setPdfStatus(const QString &text)
{
    if (m_pdfStatusLabel != nullptr) {
        m_pdfStatusLabel->setText(text);
    }
}

void PreviewWindow::showPreviousPdfPage() { goToPdfPage(m_pdfCurrentPage - 1); }

void PreviewWindow::showNextPdfPage() { goToPdfPage(m_pdfCurrentPage + 1); }

void PreviewWindow::goToPdfPage(int page)
{
    if (!m_isPdfPreview || m_pdfPageCount < 1) {
        return;
    }
    if (page < 0) {
        page = 0;
    }
    if (page >= m_pdfPageCount) {
        page = m_pdfPageCount - 1;
    }
    if (page == m_pdfCurrentPage && m_pdfCache.contains(page)) {
        updatePdfControls();
        return;
    }
    m_pdfCurrentPage = page;
    updatePdfControls();
    requestPdfPage(page);
}

void PreviewWindow::zoomPdfIn()
{
    if (!m_isPdfPreview) {
        return;
    }
    m_pdfFitMode = pdf_zoom::FitMode::Manual;
    m_pdfZoom = pdf_zoom::zoomIn(m_pdfZoom);
    updatePdfControls();
    updatePdfScrollPolicies();
    updatePdfDisplay();
}

void PreviewWindow::zoomPdfOut()
{
    if (!m_isPdfPreview) {
        return;
    }
    m_pdfFitMode = pdf_zoom::FitMode::Manual;
    m_pdfZoom = pdf_zoom::zoomOut(m_pdfZoom);
    updatePdfControls();
    updatePdfScrollPolicies();
    updatePdfDisplay();
}

void PreviewWindow::fitPdfPage()
{
    if (!m_isPdfPreview) {
        return;
    }
    // Leaving Manual for a fit restarts the fit-relative zoom at 100 %, so
    // the next step out is exactly one 25 % step of the on-screen size.
    m_pdfFitMode = pdf_zoom::FitMode::FitPage;
    m_pdfZoom = 1.0;
    updatePdfControls();
    updatePdfScrollPolicies();
    updatePdfDisplay();
}

void PreviewWindow::fitPdfWidth()
{
    if (!m_isPdfPreview) {
        return;
    }
    m_pdfFitMode = pdf_zoom::FitMode::FitWidth;
    m_pdfZoom = 1.0;
    updatePdfControls();
    updatePdfScrollPolicies();
    updatePdfDisplay();
}

void PreviewWindow::onPdfPageRendered(quint64 ticket, int page, QImage image, QString error)
{
    if (!m_isPdfPreview || ticket != m_pdfTicket.load() || page != m_pdfCurrentPage) {
        // Obsolete render (rapid navigation) or closing window: discard so
        // stale results never replace the requested page.
        return;
    }
    if (image.isNull()) {
        QString detail = error.trimmed();
        if (detail.isEmpty()) {
            detail = QStringLiteral("unreadable or corrupt");
        }
        setPdfStatus(QStringLiteral("Could not load a PDF preview of this file (%1).").arg(detail));
        updatePdfControls();
        return;
    }
    insertPdfCache(page, image);
    const QPixmap pixmap = QPixmap::fromImage(image);
    if (pixmap.isNull()) {
        setPdfStatus(QStringLiteral("Could not load a PDF preview of this file "
                                    "(unreadable or corrupt)."));
        updatePdfControls();
        return;
    }
    m_original = pixmap;
    updatePdfDisplay();
    updatePdfControls();
    setPdfStatus(pdf_zoom::statusText(m_pdfCurrentPage, m_pdfPageCount, m_original.size()));
}
#endif
