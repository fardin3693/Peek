#include "preview_request.h"
#include "preview_window.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QStringList>
#include <QTextStream>

#include <array>
#include <cstdio>

int main(int argc, char *argv[])
{
    QStringList arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        arguments.append(QString::fromLocal8Bit(argv[i]));
    }

    // Qt consumes some options even after "--". Give it only the program name
    // so filenames such as "-widgetcount" remain operands of Peek's parser.
    int qtArgc = 1;
    std::array<char *, 2> qtArgv = {argv[0], nullptr};
    QApplication app(qtArgc, qtArgv.data());
    QApplication::setApplicationName(QStringLiteral("peek"));
    QApplication::setApplicationVersion(QStringLiteral(PEEK_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Peek: lightweight Quick Look-style file previewer (image preview)."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"),
                                 QStringLiteral("Local file or directory to preview."),
                                 QStringLiteral("[PATH]"));
    if (!parser.parse(arguments)) {
        QTextStream(stderr) << "peek: " << parser.errorText() << '\n';
        return 2;
    }
    if (parser.isSet(QStringLiteral("help")) || parser.isSet(QStringLiteral("help-all"))) {
        parser.showHelp();
    }
    if (parser.isSet(QStringLiteral("version"))) {
        parser.showVersion();
    }

    const QStringList paths = parser.positionalArguments();
    if (paths.isEmpty()) {
        QTextStream out(stdout);
        out << "Peek " << QApplication::applicationVersion() << " (Qt " << qVersion()
            << ", platform: " << QApplication::platformName() << ")\n"
            << "Usage: peek [options] PATH\n"
            << "Run 'peek --help' for usage. Only image files and directories are supported.\n";
        return 0;
    }
    if (paths.size() != 1) {
        QTextStream(stderr) << "peek: expected exactly one path\n";
        return 2;
    }

    const PreviewRequestResult result = validatePreviewPath(paths.constFirst());
    if (!result.error.isEmpty()) {
        QTextStream(stderr) << "peek: " << result.error << '\n';
        return paths.constFirst().isEmpty() ? 2 : 1;
    }

    PreviewWindow window(result.request);
    window.show();
    return QApplication::exec();
}
