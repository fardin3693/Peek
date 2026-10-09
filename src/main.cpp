#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("peek"));
    QApplication::setApplicationVersion(QStringLiteral(PEEK_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Peek: lightweight Quick Look-style file previewer (scaffolding only)"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.process(app);

    QTextStream out(stdout);
    out << "Peek " << QApplication::applicationVersion() << " (Qt " << qVersion()
        << ", platform: " << QApplication::platformName() << ")\n"
        << "Milestone 1: project scaffolding only. No preview functionality yet.\n";

    // No window and no event loop yet; exit cleanly.
    return 0;
}
