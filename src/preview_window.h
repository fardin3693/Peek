#pragma once

#include <QImage>
#include <QMap>
#include <QPixmap>
#include <QSize>
#include <QWidget>

#include <atomic>

#include "pdf_zoom.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSlider;
class QTableWidget;
class QTextBrowser;
class QThread;
class QTimer;
class QVBoxLayout;
#ifdef PEEK_WITH_MULTIMEDIA
class QMediaPlayer;
#endif
#ifdef PEEK_WITH_PDF
class PdfPageWorker;
#endif

struct PreviewRequest;

class PreviewWindow : public QWidget
{
    Q_OBJECT
  public:
    explicit PreviewWindow(const PreviewRequest &request, QWidget *parent = nullptr);
    ~PreviewWindow() override;

    // True when an image was decoded and is displayed. Directories and
    // unsupported/unreadable files return false and show a status message.
    // PDF previews reuse the image view, so a successful PDF preview reports
    // true here as well as in isPdfPreview().
    bool isImagePreview() const;
    // True when a text file was loaded and is displayed in the text view.
    bool isTextPreview() const;
    // True when a Markdown file was rendered and is displayed in the
    // Markdown view. Markdown files never render as plain text.
    bool isMarkdownPreview() const;
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
    // True when an archive file claimed the preview area: a read-only
    // listing of its entries. Corrupt or unsupported archives keep the claim
    // with a status message instead of falling through to another preview.
    bool isArchivePreview() const;
#ifdef PEEK_WITH_PDF
    // True when a PDF claimed the preview area with interactive navigation
    // and zoom controls. Failed PDFs (locked/corrupt/empty) keep the claim
    // with a status message but report false here, matching the existing
    // no-view convention.
    bool isPdfPreview() const;
    // 0-based current page; valid only when isPdfPreview() is true.
    int pdfCurrentPage() const;
    int pdfPageCount() const;
    double pdfZoomFactor() const;
    pdf_zoom::FitMode pdfFitMode() const;
#endif
    QSize originalImageSize() const;

    // Aspect-preserving fit of `original` inside `bounds` (Qt::KeepAspectRatio).
    // Returns `original` when either size is empty/null.
    static QSize fittedSize(const QSize &original, const QSize &bounds);

#ifdef PEEK_WITH_PDF
  public slots:
    void showPreviousPdfPage();
    void showNextPdfPage();
    void goToPdfPage(int page);
    void zoomPdfIn();
    void zoomPdfOut();
    void fitPdfPage();
    void fitPdfWidth();

  signals:
    // Type-checked worker request (queued across threads). Prefer this over
    // stringly-typed QMetaObject::invokeMethod so a signature drift fails at
    // compile time instead of silently disabling navigation.
    void pdfRenderRequested(const QString &path, int page, quint64 ticket);

  private slots:
    void onPdfPageRendered(quint64 ticket, int page, QImage image, QString error);
#endif

  protected:
#ifdef PEEK_WITH_PDF
    bool eventFilter(QObject *watched, QEvent *event) override;
#endif
    void resizeEvent(QResizeEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

  private:
    // Fast mode keeps interactive drags cheap on the UI thread; a
    // single-shot timer schedules one smooth pass once the resize settles.
    // Smooth passes are skipped when the available size is unchanged.
    void updateScaledPixmap(Qt::TransformationMode mode = Qt::SmoothTransformation);
    void scheduleSmoothRescale();

#ifdef PEEK_WITH_MULTIMEDIA
    // Claims the preview area for a video file: streaming QMediaPlayer output
    // plus play/pause and seek controls. Always returns true so unsupported
    // codecs, missing backends, and invalid files report through the video
    // status line instead of falling through to unrelated previews.
    bool showVideoFile(const QString &path, QLabel *statusLabel, QVBoxLayout *layout);
#endif

#ifdef PEEK_WITH_PDF
    // Wires Quick Look-style navigation/zoom onto an already-rendered first
    // page: compact controls, page indicator, bounded cache, background
    // rendering, and keyboard shortcuts. Called once per PDF window.
    void setupPdfInteraction(const QString &path, int pageCount, QLabel *statusLabel,
                             QVBoxLayout *layout);
    void updatePdfControls();
    // Scales the cached base pixmap for the current fit/zoom mode into the
    // scroll viewport. Pure display scaling: never reloads the document.
    // Percentages are fit-relative (100 % == the fitted size); see
    // pdfDisplayTarget().
    void updatePdfDisplay(Qt::TransformationMode mode = Qt::SmoothTransformation);
    // Current on-screen target shared by updatePdfDisplay() and the zoom
    // steps so the two can never disagree. FitPage/FitWidth fit the base
    // into the viewport; Manual scales that fit by the zoom factor.
    QSize pdfDisplayTarget() const;
    void requestPdfPage(int page);
    void insertPdfCache(int page, const QImage &image);
    void startPdfThread();
    void stopPdfThread();
    void schedulePdfSmoothRescale();
    void updatePdfScrollPolicies();
    void setPdfStatus(const QString &text);
#endif

    // Owned by the image provider's view (parented to the window); cached
    // here after a successful render so resize scaling stays O(1). PDF
    // previews reuse this label inside their scroll area.
    QLabel *m_imageLabel = nullptr;
    // Owned by the text provider's view (parented to the window); cached here
    // after a successful render so isTextPreview() stays O(1).
    QPlainTextEdit *m_textView = nullptr;
    // Owned by the Markdown provider's view (parented to the window); cached
    // here after a successful render so isMarkdownPreview() stays O(1).
    QTextBrowser *m_markdownView = nullptr;
    // Owned by the archive provider's view (parented to the window); cached
    // here after a successful render so isArchivePreview() stays O(1).
    QTableWidget *m_archiveView = nullptr;
    QPixmap m_original;
    // Last label size rendered with SmoothTransformation; fast drag previews
    // never update it so the debounced smooth pass still runs.
    QSize m_lastSmoothAvailable;
    QTimer *m_smoothTimer = nullptr;
#ifdef PEEK_WITH_MULTIMEDIA
    QMediaPlayer *m_videoPlayer = nullptr;
    QPushButton *m_playButton = nullptr;
    QSlider *m_seekSlider = nullptr;
    QLabel *m_positionLabel = nullptr;
    // Owned by the audio provider's view (parented to the window); cached
    // here after a successful render so isAudioPreview() stays O(1) and
    // close/destruction can stop the streaming player.
    QMediaPlayer *m_audioPlayer = nullptr;
    QPushButton *m_audioPlayButton = nullptr;
    QSlider *m_audioSeekSlider = nullptr;
    QLabel *m_audioPositionLabel = nullptr;
#endif
#ifdef PEEK_WITH_PDF
    bool m_isPdfPreview = false;
    QString m_pdfPath;
    int m_pdfPageCount = 0;
    int m_pdfCurrentPage = 0;
    // Fit-relative zoom multiplier (1.0 == the fitted size, clamped to
    // 25 %–400 %). Reset to 1.0 whenever a fit mode is entered so the first
    // step out of a fit is exactly one 25 % step of what is on screen.
    double m_pdfZoom = 1.0;
    pdf_zoom::FitMode m_pdfFitMode = pdf_zoom::FitMode::FitPage;
    // QImage (not QPixmap): client-side pixels only, converted once for the
    // visible page, so the cache holds no server/GPU-side resources.
    QMap<int, QImage> m_pdfCache;
    // Generation ticket: the UI thread writes it on every request (and on
    // close/teardown); the worker reads it before rasterizing to skip stale
    // jobs, and the completion slot rechecks it to discard stale results.
    std::atomic<quint64> m_pdfTicket{0};
    // Cached once at setup; every status update goes through setPdfStatus()
    // instead of re-walking the object tree.
    QLabel *m_pdfStatusLabel = nullptr;
    QWidget *m_pdfControls = nullptr;
    QPushButton *m_pdfPrevButton = nullptr;
    QPushButton *m_pdfNextButton = nullptr;
    QPushButton *m_pdfZoomInButton = nullptr;
    QPushButton *m_pdfZoomOutButton = nullptr;
    QPushButton *m_pdfFitPageButton = nullptr;
    QPushButton *m_pdfFitWidthButton = nullptr;
    QLabel *m_pdfPageLabel = nullptr;
    QLabel *m_pdfZoomLabel = nullptr;
    QScrollArea *m_pdfScrollArea = nullptr;
    QThread *m_pdfThread = nullptr;
    PdfPageWorker *m_pdfWorker = nullptr;
    // Debounced smooth pass for PDF fit modes, mirroring the image path's
    // fast-during-drag + ~100 ms smooth-settle pattern (see O5).
    QTimer *m_pdfSmoothTimer = nullptr;
#endif
};

#ifdef PEEK_WITH_PDF
// Background PDF rasterizer: lives in the preview's worker thread, renders
// exactly one requested page per task with its own Poppler document instance
// (no shared state), then reports back. Never touches widgets or pixmaps;
// the window converts QImage to QPixmap on the UI thread.
class PdfPageWorker : public QObject
{
    Q_OBJECT
  public:
    explicit PdfPageWorker(QObject *parent = nullptr) : QObject(parent) {}

    // Latest generation ticket, owned by the window. The worker compares each
    // queued task against it *before* rasterizing and returns immediately for
    // superseded pages, so rapid navigation wastes no full renders. Safe to
    // read from the worker thread (atomic); the window guarantees the worker
    // is stopped before either object is destroyed.
    std::atomic<quint64> *latestTicket = nullptr;

  public slots:
    void render(const QString &path, int page, quint64 ticket);

  signals:
    void done(quint64 ticket, int page, QImage image, QString error);
};
#endif
