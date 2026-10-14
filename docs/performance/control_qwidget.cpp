// P-START1 minimal Qt control — NOT part of Peek.
//
// QApplication + one QLabel window + show. Timed by the same harness and
// the same signals (CTOR/SHOW/PAINT/openwindow) as the Peek timeline
// probe, to isolate the shared Qt/platform/compositor baseline. It is NOT
// equivalent to Peek: no parsing, no validation, no providers, no Poppler,
// no Multimedia linkage, one label instead of Peek's window. Differences
// are accounted in the report; the control bounds the shared floor only.
#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QObject>

#include <cstdio>
#include <cstdlib>
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
class PaintSpy : public QObject
{
protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (!m_done && event->type() == QEvent::Paint) {
            m_done = true;
            std::printf("STAGE_PAINT %.3f\n", (nowNs() - g_start) / 1e6);
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
        return 2;
    }
    g_start = std::strtoll(raw, nullptr, 10);
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("peekctl"));
    const long long begin = nowNs();
    QLabel window(QStringLiteral("control"));
    window.setWindowTitle(QStringLiteral("PeekCtl — control"));
    std::printf("STAGE_CTOR %.3f\n", (nowNs() - begin) / 1e6);
    PaintSpy spy;
    app.installEventFilter(&spy);
    window.show();
    std::printf("STAGE_SHOW %.3f\n", (nowNs() - g_start) / 1e6);
    std::fflush(stdout);
    return QApplication::exec();
}
