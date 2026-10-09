#include "preview_request.h"
#include "preview_window.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <array>

#ifdef Q_OS_LINUX
#include <sys/stat.h>
#endif

namespace
{

struct FixtureName {
    const char *row;
    QString name;
};

void addFixtureRows()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("isDirectory");
    QTest::addColumn<bool>("isRelative");

    const std::array<FixtureName, 9> names = {{
        {"ordinary", QStringLiteral("ordinary.txt")},
        {"spaces", QStringLiteral("name with spaces.txt")},
        {"quotes", QStringLiteral("single ' and double \" quotes.txt")},
        {"unicode", QStringLiteral("café 日本語 😀.txt")},
        {"markup", QStringLiteral("<b>markup &amp;.txt")},
        {"semicolon", QStringLiteral("semi;colon.txt")},
        {"newline", QStringLiteral("line\nbreak.txt")},
        {"leading-dash", QStringLiteral("-leading-dash.txt")},
        {"leading-colon", QStringLiteral(":notes.txt")},
    }};

    for (const auto &fixture : names) {
        for (const bool isDirectory : {false, true}) {
            for (const bool isRelative : {false, true}) {
                const QByteArray row = QByteArray(fixture.row) +
                                       (isDirectory ? "-directory" : "-file") +
                                       (isRelative ? "-relative" : "-absolute");
                QTest::newRow(row.constData()) << fixture.name << isDirectory << isRelative;
            }
        }
    }
}

void addRelativeRows()
{
    QTest::addColumn<bool>("isRelative");
    QTest::newRow("absolute") << false;
    QTest::newRow("relative") << true;
}

void addTypeRows()
{
    QTest::addColumn<bool>("isDirectory");
    QTest::newRow("file") << false;
    QTest::newRow("directory") << true;
}

bool createFixture(const QString &path, bool isDirectory)
{
    if (isDirectory) {
        return QDir().mkdir(path);
    }

    QFile file(path);
    const QByteArray contents("test-owned preview fixture\n");
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QString inputPath(const QString &absolutePath, bool isRelative)
{
    return isRelative ? QDir::current().relativeFilePath(absolutePath) : absolutePath;
}

QString absoluteSpelling(const QString &path)
{
    if (path.startsWith(QLatin1Char('/'))) {
        return path;
    }

    QString base = QDir::currentPath();
    if (!base.endsWith(QLatin1Char('/'))) {
        base += QLatin1Char('/');
    }
    return base + path;
}

void checkWindow(const PreviewRequest &request, QString name = {})
{
    PreviewWindow window(request);
    if (name.isEmpty()) {
        name = QFileInfo(QDir::cleanPath(request.path)).fileName();
    }
    if (name.isEmpty()) {
        name = request.path;
    }

    QCOMPARE(window.windowTitle(), QStringLiteral("Peek — %1").arg(name));

    auto *fileName = window.findChild<QLabel *>(QStringLiteral("fileNameLabel"));
    auto *path = window.findChild<QLabel *>(QStringLiteral("pathLabel"));
    auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
    QVERIFY(fileName != nullptr);
    QVERIFY(path != nullptr);
    QVERIFY(status != nullptr);

    QCOMPARE(fileName->text(), name);
    QCOMPARE(path->text(), request.path);
    QCOMPARE(status->text(), request.isDirectory
                                 ? QStringLiteral("Directory preview not implemented yet.")
                                 : QStringLiteral("Preview rendering not implemented yet."));
    for (const auto *label : {fileName, path, status}) {
        QCOMPARE(label->textFormat(), Qt::PlainText);
        QVERIFY(label->wordWrap());
    }
}

} // namespace

class PreviewTests : public QObject
{
    Q_OBJECT

  private slots:
    void validPaths_data() { addFixtureRows(); }

    void validPaths()
    {
        QFETCH(QString, name);
        QFETCH(bool, isDirectory);
        QFETCH(bool, isRelative);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString absolutePath = fixtures.path() + QLatin1Char('/') + name;
        QVERIFY(createFixture(absolutePath, isDirectory));
        const QString path = inputPath(absolutePath, isRelative);
        QCOMPARE(QDir::isRelativePath(path), isRelative);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, absoluteSpelling(path));
        QVERIFY(QDir::isAbsolutePath(result.request.path));
        QCOMPARE(result.request.isDirectory, isDirectory);
    }

    void bareColonPath()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.path() + QStringLiteral("/:notes.txt");
        QVERIFY(createFixture(path, false));
        const QString originalDirectory = QDir::currentPath();
        QVERIFY(QDir::setCurrent(fixtures.path()));
        const auto bare = validatePreviewPath(QStringLiteral(":notes.txt"));
        const auto dotted = validatePreviewPath(QStringLiteral("./:notes.txt"));
        const bool restored = QDir::setCurrent(originalDirectory);
        QVERIFY(restored);
        QVERIFY2(bare.error.isEmpty(), qPrintable(bare.error));
        QVERIFY2(dotted.error.isEmpty(), qPrintable(dotted.error));
        QCOMPARE(bare.request.path, path);
        QCOMPARE(QFileInfo(bare.request.path).canonicalFilePath(),
                 QFileInfo(dotted.request.path).canonicalFilePath());
        QVERIFY(!bare.request.isDirectory);
    }

    void emptyPath()
    {
        QCOMPARE(validatePreviewPath(QString()).error, QStringLiteral("empty path"));
    }

    void missingPath_data() { addRelativeRows(); }

    void missingPath()
    {
        QFETCH(bool, isRelative);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = inputPath(
            fixtures.filePath(QStringLiteral("missing ' \" 日本語 <b>; line\n.txt")), isRelative);
        QVERIFY(!QFileInfo::exists(path));

        const auto result = validatePreviewPath(path);
        QCOMPARE(result.error, QStringLiteral("path does not exist or cannot be accessed: %1")
                                   .arg(absoluteSpelling(path)));
    }

    void symlinkPath_data()
    {
        QTest::addColumn<bool>("isDirectory");
        QTest::addColumn<bool>("isRelative");
        QTest::newRow("file-absolute") << false << false;
        QTest::newRow("file-relative") << false << true;
        QTest::newRow("directory-absolute") << true << false;
        QTest::newRow("directory-relative") << true << true;
    }

    void symlinkPath()
    {
#ifdef Q_OS_LINUX
        QFETCH(bool, isDirectory);
        QFETCH(bool, isRelative);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString target = fixtures.filePath(QStringLiteral("target.txt"));
        const QString link = fixtures.filePath(QStringLiteral("link ' \" 日本語 <b>; line\n.txt"));
        QVERIFY(createFixture(target, isDirectory));
        QVERIFY(QFile::link(target, link));
        QVERIFY(QFileInfo(link).isSymbolicLink());

        const QString path = inputPath(link, isRelative);
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, absoluteSpelling(path));
        if (!isRelative) {
            QCOMPARE(result.request.path, link);
        }
        QVERIFY(result.request.path != QFileInfo(link).canonicalFilePath());
        QCOMPARE(QFileInfo(result.request.path).canonicalFilePath(),
                 QFileInfo(target).canonicalFilePath());
        QCOMPARE(result.request.isDirectory, isDirectory);
        checkWindow(result.request);
#else
        QSKIP("These symlink fixtures use Linux QFile::link semantics.");
#endif
    }

    void symlinkParentTraversal_data()
    {
        QTest::addColumn<bool>("isRelative");
        QTest::addColumn<bool>("hasDecoyDirectory");
        QTest::newRow("absolute-decoy-directory") << false << true;
        QTest::newRow("relative-decoy-directory") << true << true;
        QTest::newRow("absolute-decoy-missing") << false << false;
        QTest::newRow("relative-decoy-missing") << true << false;
    }

    void symlinkParentTraversal()
    {
#ifdef Q_OS_LINUX
        QFETCH(bool, isRelative);
        QFETCH(bool, hasDecoyDirectory);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString base = fixtures.filePath(QStringLiteral("base"));
        const QString other = fixtures.filePath(QStringLiteral("other"));
        const QString subdir = other + QStringLiteral("/subdir");
        const QString actualItem = other + QStringLiteral("/item");
        const QString decoyItem = base + QStringLiteral("/item");
        const QString link = base + QStringLiteral("/link");
        QVERIFY(QDir().mkdir(base));
        QVERIFY(QDir().mkpath(subdir));
        QVERIFY(createFixture(actualItem, false));
        if (hasDecoyDirectory) {
            QVERIFY(createFixture(decoyItem, true));
        } else {
            QVERIFY(!QFileInfo::exists(decoyItem));
        }
        QVERIFY(QFile::link(subdir, link));
        QVERIFY(QFileInfo(link).isSymbolicLink());

        // Append traversal after making the link relative: relativeFilePath itself
        // cleans dot segments, which would invalidate this regression fixture.
        const QString path = inputPath(link, isRelative) + QStringLiteral("/../item");
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, absoluteSpelling(path));
        QVERIFY(result.request.path.contains(QStringLiteral("/link/../item")));
        QVERIFY(!result.request.isDirectory);
        QVERIFY(QFileInfo(result.request.path).isFile());

        const QString actualTarget = QFileInfo(actualItem).canonicalFilePath();
        QVERIFY(!actualTarget.isEmpty());
        QCOMPARE(QFileInfo(path).canonicalFilePath(), actualTarget);
        QCOMPARE(QFileInfo(result.request.path).canonicalFilePath(), actualTarget);
        QVERIFY(QFileInfo(QDir::cleanPath(result.request.path)).canonicalFilePath() !=
                actualTarget);
        checkWindow(result.request, QStringLiteral("item"));
#else
        QSKIP("These symlink fixtures use Linux QFile::link semantics.");
#endif
    }

    void brokenSymlink_data() { addRelativeRows(); }

    void brokenSymlink()
    {
#ifdef Q_OS_LINUX
        QFETCH(bool, isRelative);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString target = fixtures.filePath(QStringLiteral("target.txt"));
        const QString link = fixtures.filePath(QStringLiteral("broken link.txt"));
        QVERIFY(createFixture(target, false));
        QVERIFY(QFile::link(target, link));
        QVERIFY(QFile::remove(target));
        QVERIFY(QFileInfo(link).isSymbolicLink());
        QVERIFY(!QFileInfo::exists(link));

        const QString path = inputPath(link, isRelative);
        const auto result = validatePreviewPath(path);
        QCOMPARE(result.error, QStringLiteral("path does not exist or cannot be accessed: %1")
                                   .arg(absoluteSpelling(path)));
#else
        QSKIP("These symlink fixtures use Linux QFile::link semantics.");
#endif
    }

    void fifoPath_data() { addRelativeRows(); }

    void fifoPath()
    {
#ifdef Q_OS_LINUX
        QFETCH(bool, isRelative);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString fifo = fixtures.filePath(QStringLiteral("fifo with spaces; 日本語"));
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        QVERIFY(QFileInfo::exists(fifo));
        QVERIFY(!QFileInfo(fifo).isFile());
        QVERIFY(!QFileInfo(fifo).isDir());

        const QString path = inputPath(fifo, isRelative);
        QCOMPARE(validatePreviewPath(path).error,
                 QStringLiteral("unsupported file type: %1").arg(absoluteSpelling(path)));
#else
        QSKIP("FIFO fixtures require Linux mkfifo.");
#endif
    }

    void windowLabels_data() { addFixtureRows(); }

    void windowLabels()
    {
        QFETCH(QString, name);
        QFETCH(bool, isDirectory);
        QFETCH(bool, isRelative);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString absolutePath = fixtures.path() + QLatin1Char('/') + name;
        QVERIFY(createFixture(absolutePath, isDirectory));
        const QString path = inputPath(absolutePath, isRelative);
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, absoluteSpelling(path));
        QCOMPARE(result.request.isDirectory, isDirectory);
        checkWindow(result.request);
    }

    void directoryDisplay_data()
    {
        QTest::addColumn<bool>("isRelative");
        QTest::addColumn<QString>("suffix");
        QTest::addColumn<QString>("expectedName");
        const QString name = QStringLiteral("display directory");
        QTest::newRow("absolute-trailing-slash") << false << QStringLiteral("/") << name;
        QTest::newRow("relative-trailing-slash") << true << QStringLiteral("/") << name;
        QTest::newRow("absolute-trailing-dot-slash") << false << QStringLiteral("/./") << name;
        QTest::newRow("relative-trailing-dot-slash") << true << QStringLiteral("/./") << name;
    }

    void directoryDisplay()
    {
        QFETCH(bool, isRelative);
        QFETCH(QString, suffix);
        QFETCH(QString, expectedName);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString directory = fixtures.filePath(QStringLiteral("display directory"));
        QVERIFY(createFixture(directory, true));
        const QString path = inputPath(directory, isRelative) + suffix;
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, absoluteSpelling(path));
        QVERIFY(result.request.path.endsWith(suffix));
        QVERIFY(result.request.isDirectory);
        QCOMPARE(QFileInfo(result.request.path).canonicalFilePath(),
                 QFileInfo(directory).canonicalFilePath());
        checkWindow(result.request, expectedName);
    }

    void longPathWindow()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QString(240, QLatin1Char('x')));
        QVERIFY(createFixture(path, false));
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY2(window.width() <= 640, "A long unbroken filename must not force a wide window.");
        checkWindow(result.request);
    }

    void filesystemRoot()
    {
        const QString root = QDir::rootPath();
        const auto result = validatePreviewPath(root);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.request.path, root);
        QVERIFY(result.request.isDirectory);
        checkWindow(result.request);
    }

    void escapeClosesWindow_data() { addTypeRows(); }

    void escapeClosesWindow()
    {
        QFETCH(bool, isDirectory);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("escape fixture"));
        QVERIFY(createFixture(path, isDirectory));
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(window.isVisible());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
    }

    void ordinaryClose_data() { addTypeRows(); }

    void ordinaryClose()
    {
        QFETCH(bool, isDirectory);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("close fixture"));
        QVERIFY(createFixture(path, isDirectory));
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        window.show();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
    }

    void dismissalQuitsEventLoop_data()
    {
        QTest::addColumn<bool>("useEscape");
        QTest::newRow("escape") << true;
        QTest::newRow("ordinary-close") << false;
    }

    void dismissalQuitsEventLoop()
    {
        QFETCH(bool, useEscape);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("event loop fixture"));
        QVERIFY(createFixture(path, false));
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(QApplication::quitOnLastWindowClosed());

        QTimer watchdog;
        watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, [] { QApplication::exit(1); });
        watchdog.start(2000);
        bool dismissed = false;
        QTimer::singleShot(0, &window, [&] {
            if (useEscape) {
                QTest::keyClick(&window, Qt::Key_Escape);
            } else {
                window.close();
            }
            dismissed = !window.isVisible();
        });
        QCOMPARE(QApplication::exec(), 0);
        QVERIFY(dismissed);
    }

    void cliValidPath_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("isDirectory");
        QTest::addColumn<bool>("isRelative");
        QTest::addColumn<bool>("useSeparator");
        const QString unusual = QStringLiteral("space ' \" 日本語 <b>; line\n.txt");
        QTest::newRow("absolute-file") << unusual << false << false << false;
        QTest::newRow("relative-file") << unusual << false << true << false;
        QTest::newRow("relative-directory") << unusual << true << true << false;
        QTest::newRow("leading-dash-after-separator")
            << QStringLiteral("-leading dash; 日本語.txt") << false << true << true;
        QTest::newRow("qt-widgetcount-after-separator")
            << QStringLiteral("-widgetcount") << false << true << true;
        QTest::newRow("qt-platform-after-separator")
            << QStringLiteral("-platform") << false << true << true;
        QTest::newRow("qt-style-after-separator")
            << QStringLiteral("-style") << false << true << true;
        QTest::newRow("colon-file") << QStringLiteral(":notes.txt") << false << true << false;
        QTest::newRow("colon-file-after-separator")
            << QStringLiteral(":notes.txt") << false << true << true;
    }

    void cliValidPath()
    {
        QFETCH(QString, name);
        QFETCH(bool, isDirectory);
        QFETCH(bool, isRelative);
        QFETCH(bool, useSeparator);
        const QString executable = qEnvironmentVariable("PEEK_TEST_EXECUTABLE");
        QVERIFY2(!executable.isEmpty(), "CTest must supply PEEK_TEST_EXECUTABLE.");
        QVERIFY(QFileInfo(executable).isExecutable());
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.path() + QLatin1Char('/') + name;
        QVERIFY(createFixture(path, isDirectory));

        QStringList arguments;
        if (useSeparator) {
            arguments.append(QStringLiteral("--"));
        }
        arguments.append(isRelative ? name : path);
        QProcess process;
        process.setProgram(executable);
        process.setArguments(arguments);
        process.setWorkingDirectory(fixtures.path());
        process.start();
        const bool started = process.waitForStarted(3000);
        const QString startError = process.errorString();
        if (!started && process.state() != QProcess::NotRunning) {
            process.kill();
            QVERIFY2(process.waitForFinished(2000),
                     "Could not reap the CLI process after startup failed.");
        }
        QVERIFY2(started, qPrintable(startError));

        // This bounded offscreen check covers argv acceptance and event-loop lifetime,
        // not compositor integration or which preview the native window displays.
        const bool finishedEarly = process.waitForFinished(750);
        const bool stayedRunning = !finishedEarly && process.state() == QProcess::Running;
        const QByteArray diagnostics =
            process.readAllStandardOutput() + process.readAllStandardError();
        if (process.state() != QProcess::NotRunning) {
            process.terminate();
            if (!process.waitForFinished(2000)) {
                process.kill();
                QVERIFY2(process.waitForFinished(2000), "Could not reap the killed CLI process.");
            }
        }
        QCOMPARE(process.state(), QProcess::NotRunning);
        QVERIFY2(stayedRunning, diagnostics.constData());
    }
};

QTEST_MAIN(PreviewTests)
#include "test_preview.moc"
