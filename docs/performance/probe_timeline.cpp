// P-START1 temporary timeline diagnostic — NOT part of Peek.
//
// Compiled by docs/performance/diag_start1.sh into /tmp at bench time.
// Never in CMake/CTest, never shipped. Links the unmodified Peek sources
// and replicates main()'s startup order (argv save → QApplication →
// QCommandLineParser → validatePreviewPath → PreviewWindow → show → exec)
// with a monotonic timestamp after each stage. All STAGE_* lines are
// milliseconds relative to the parent's pre-spawn clock (PEEK_T0_NS).
//
// Stages map to the P-START1 brief: QAPP covers QApplication construction
// plus platform/theme init; SUPPORTS_* times each provider's cheap
// extension check individually; CTOR covers the claiming provider's
// synchronous render; SHOW is show() returning; PAINT is the first
// QEvent::Paint on any widget (parent harness adds compositor mapping
// and settled-frame screenshots).
#include "preview_request.h"
#include "preview_window.h"

#include "audio_preview.h"
#include "image_provider.h"
#include "text_provider.h"
#include "video_preview.h"
#ifdef PEEK_WITH_PDF
#include "pdf_provider.h"
#endif
#ifdef PEEK_WITH_MULTIMEDIA
#include "audio_provider.h"
#endif

#include <QApplication>
#include <QCommandLineParser>
#include <QEvent>
#include <QObject>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace
{

long long nowNs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

long long g_start = 0;

void stamp(const char *stage) { std::printf("%s %.3f\n", stage, (nowNs() - g_start) / 1e6); }

void stampSupports(const char *label, ProviderSupport (*fn)(const QString &),
                   const QString &path)
{
    const long long begin = nowNs();
    volatile ProviderSupport sink = fn(path); // keep the call, drop the result
    (void)sink;
    std::printf("%s %.3f\n", label,
                static_cast<double>(nowNs() - begin) / 1e6);
}

class PaintSpy : public QObject
{
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (!m_done && event->type() == QEvent::Paint) {
            m_done = true;
            stamp("STAGE_PAINT");
            std::fflush(stdout);
        }
        return false;
    }

private:
    bool m_done = false;
};

} // namespace

int main(int argc, char *argv[])
{
    const char *raw = std::getenv("PEEK_T0_NS");
    if (raw == nullptr || *raw == '\0') {
        std::fprintf(stderr, "probe: PEEK_T0_NS (monotonic ns) is required\n");
        return 2;
    }
    g_start = std::strtoll(raw, nullptr, 10);
    stamp("STAGE_MAIN");

    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }
    int qtArgc = 1;
    char *qtArgv[2] = {argv[0], nullptr};
    QApplication app(qtArgc, qtArgv);
    QApplication::setApplicationName(QStringLiteral("peek"));
    QApplication::setApplicationVersion(QStringLiteral(PEEK_VERSION));
    stamp("STAGE_QAPP");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Peek timeline probe"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("path"),
                                 QStringLiteral("[PATH]"));
    if (!parser.parse(arguments)) {
        return 2;
    }
    const QStringList paths = parser.positionalArguments();
    if (paths.size() != 1) {
        return 2;
    }
    stamp("STAGE_PARSE");

    const PreviewRequestResult result = validatePreviewPath(paths.constFirst());
    if (!result.error.isEmpty()) {
        return 1;
    }
    stamp("STAGE_VALIDATE");

    // Stage 4 in isolation: each dispatch check on its own clock.
    stampSupports("SUPPORT_TEXT", textProvider().supports, result.request.path);
#ifdef PEEK_WITH_PDF
    stampSupports("SUPPORT_PDF", pdfProvider().supports, result.request.path);
#endif
    stampSupports("SUPPORT_AUDIO", [](const QString &p) {
        return isAudioFile(p) ? ProviderSupport::Yes : ProviderSupport::No;
    }, result.request.path);
    stampSupports("SUPPORT_VIDEO", [](const QString &p) {
        return isVideoFile(p) ? ProviderSupport::Yes : ProviderSupport::No;
    }, result.request.path);
    stampSupports("SUPPORT_IMAGE", imageProvider().supports, result.request.path);

    PreviewWindow window(result.request);
    stamp("STAGE_CTOR");

    PaintSpy spy;
    app.installEventFilter(&spy);
    window.show();
    stamp("STAGE_SHOW");
    std::fflush(stdout);
    return QApplication::exec();
}
