// P-PERF2 temporary first-paint diagnostic — NOT part of Peek.
//
// Compiled by docs/performance/visible.sh into /tmp/peek-perf2/ at bench
// time. Never added to CMake/CTest, never shipped, never run at preview
// time outside this benchmark. It links the same unmodified Peek sources
// as the release binary and adds only timing scaffolding:
//
//   CTOR_MS     validator + PreviewWindow construction (synchronous decode)
//   FIRST_PAINT first QEvent::Paint reaching any widget after show()
//               (decode already done in the ctor for text/image/PDF;
//               media windows paint their paused controls)
//   openwindow  recorded by the parent harness from the Hyprland socket2
//               event stream (compositor mapped the window)
//
// FIRST_PAINT is the first Qt paint request, not proof of photons: the
// compositor may map the surface before/after the client commits the frame.
// The harness screenshots both moments (see visible.sh).
#include "preview_request.h"
#include "preview_window.h"

#include <QApplication>
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

// App-wide filter: the first Paint event on any widget after show() is the
// head of Qt's first frame. Children (text view, image label, controls)
// paint as their own objects, so filtering only the top window would miss
// the actual content paint.
class PaintSpy : public QObject
{
public:
    explicit PaintSpy(long long startNs, QObject *parent = nullptr)
        : QObject(parent), m_start(startNs)
    {
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (!m_done && event->type() == QEvent::Paint) {
            m_done = true;
            std::printf("FIRST_PAINT %.3f\n",
                        static_cast<double>(nowNs() - m_start) / 1e6);
            std::fflush(stdout);
        }
        return false;
    }

private:
    long long m_start = 0;
    bool m_done = false;
};

long long envStartNs()
{
    const char *raw = std::getenv("PEEK_T0_NS");
    if (raw == nullptr || *raw == '\0') {
        return 0;
    }
    errno = 0;
    const long long value = std::strtoll(raw, nullptr, 10);
    return errno == 0 ? value : 0;
}

} // namespace

int main(int argc, char *argv[])
{
    const long long startNs = envStartNs();
    if (startNs == 0) {
        std::fprintf(stderr, "probe: PEEK_T0_NS (monotonic ns) is required\n");
        return 2;
    }

    QApplication app(argc, argv);
    // Same identity as the real app so the compositor class/app-id matches.
    QApplication::setApplicationName(QStringLiteral("peek"));
    QApplication::setApplicationVersion(QStringLiteral(PEEK_VERSION));

    // Mirror Peek's `--` operand convention (single path only).
    int pathIndex = 1;
    if (argc > 2 && std::strcmp(argv[1], "--") == 0) {
        pathIndex = 2;
    }
    if (argc - pathIndex != 1) {
        std::fprintf(stderr, "probe: expected exactly one path\n");
        return 2;
    }
    const long long ctorBegin = nowNs();
    const PreviewRequestResult result =
        validatePreviewPath(QString::fromLocal8Bit(argv[pathIndex]));
    if (!result.error.isEmpty()) {
        std::fprintf(stderr, "probe: %s\n", qPrintable(result.error));
        return 1;
    }
    PreviewWindow window(result.request);
    const double ctorMs = static_cast<double>(nowNs() - ctorBegin) / 1e6;
    std::printf("CTOR_MS %.3f\n", ctorMs);
    std::fflush(stdout);

    PaintSpy spy(startNs);
    app.installEventFilter(&spy);
    window.show();
    return QApplication::exec();
}
