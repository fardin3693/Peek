#include "preview_request.h"
#include "preview_window.h"
#include "text_preview.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
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

bool writeTestImage(const QString &path, const QSize &size, const char *format)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    return image.save(path, format);
}

#ifdef PEEK_WITH_PDF
// Assembles a minimal deterministic single-page PDF in memory: catalog,
// page tree, one Helvetica page, and a correct cross-reference table with
// computed offsets. No external producer or downloaded fixture needed.
QByteArray buildMinimalPdf()
{
    const QByteArray stream("BT /F1 24 Tf 72 720 Td (Hello Peek) Tj ET");
    const QList<QByteArray> objects = {
        QByteArray("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"),
        QByteArray("2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"),
        QByteArray("3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 300] "
                   "/Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n"),
        QByteArray("4 0 obj\n<< /Length ") + QByteArray::number(stream.size()) +
            QByteArray(" >>\nstream\n") + stream + QByteArray("\nendstream\nendobj\n"),
        QByteArray("5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"),
    };
    QByteArray pdf("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
    QVector<int> offsets;
    for (const QByteArray &object : objects) {
        offsets.append(pdf.size());
        pdf += object;
    }
    const int xrefPos = pdf.size();
    pdf += QByteArray("xref\n0 ") + QByteArray::number(objects.size() + 1) +
           QByteArray("\n0000000000 65535 f \n");
    for (const int offset : offsets) {
        pdf += QStringLiteral("%1 00000 n \n").arg(offset, 10, 10, QLatin1Char('0')).toLatin1();
    }
    pdf += QByteArray("trailer\n<< /Size ") + QByteArray::number(objects.size() + 1) +
           QByteArray(" /Root 1 0 R >>\nstartxref\n") + QByteArray::number(xrefPos) +
           QByteArray("\n%%EOF\n");
    return pdf;
}
#endif

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
    if (request.isDirectory) {
        QCOMPARE(status->text(), QStringLiteral("Directory preview not implemented yet."));
    } else if (isTextFile(request.path)) {
        // Text preview: a status line plus a read-only view holding the exact
        // file content.
        QVERIFY(status->text().contains(QStringLiteral("Text file")));
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        QVERIFY(textView != nullptr);
        QVERIFY(textView->isReadOnly());
        QFile file(request.path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(textView->toPlainText(), QString::fromUtf8(file.readAll()));
    } else {
        QCOMPARE(status->text(), QStringLiteral("No image preview is available for this file."));
    }
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

    void imageLoad_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<QByteArray>("format");
        QTest::newRow("png") << QStringLiteral("photo.png") << QByteArray("png");
        QTest::newRow("jpeg") << QStringLiteral("photo.jpg") << QByteArray("jpg");
        QTest::newRow("webp") << QStringLiteral("photo.webp") << QByteArray("webp");
    }

    void imageLoad()
    {
        QFETCH(QString, fileName);
        QFETCH(QByteArray, format);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(fileName);
        QVERIFY2(writeTestImage(path, QSize(64, 48), format.constData()),
                 "Qt could not write the test image; check installed image plugins.");
        QVERIFY(QImageReader(path).canRead());

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.request.isDirectory);

        PreviewWindow window(result.request);
        QVERIFY(window.isImagePreview());
        QCOMPARE(window.originalImageSize(), QSize(64, 48));

        auto *image = window.findChild<QLabel *>(QStringLiteral("imageLabel"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(image != nullptr);
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("64 × 48")));
        QVERIFY(window.windowTitle().contains(QFileInfo(path).fileName()));

        window.show();
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("imageLabel"))->pixmap().isNull());
    }

    void unsupportedFile()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("notes.bin"));
        QVERIFY(createFixture(path, false));

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isImagePreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->text(), QStringLiteral("No image preview is available for this file."));
    }

    void corruptImage()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.png"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not image data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        // Either "no preview" or "could not load" is acceptable; both must be
        // non-empty, plain text, and must not crash or show an image.
        QVERIFY(!window.isImagePreview());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(!status->text().isEmpty());
        QVERIFY(status->text().contains(QStringLiteral("image preview")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void textLoad_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::newRow("txt") << QStringLiteral("notes.txt");
        QTest::newRow("md") << QStringLiteral("notes.md");
        QTest::newRow("markdown") << QStringLiteral("notes.markdown");
        QTest::newRow("json") << QStringLiteral("data.json");
        QTest::newRow("jsonl") << QStringLiteral("data.jsonl");
        QTest::newRow("yaml") << QStringLiteral("data.yaml");
        QTest::newRow("yml") << QStringLiteral("data.yml");
        QTest::newRow("toml") << QStringLiteral("config.toml");
        QTest::newRow("csv") << QStringLiteral("data.csv");
        QTest::newRow("tsv") << QStringLiteral("data.tsv");
        QTest::newRow("log") << QStringLiteral("app.log");
        QTest::newRow("xml") << QStringLiteral("data.xml");
        QTest::newRow("html") << QStringLiteral("page.html");
        QTest::newRow("css") << QStringLiteral("style.css");
        QTest::newRow("c") << QStringLiteral("main.c");
        QTest::newRow("h") << QStringLiteral("main.h");
        QTest::newRow("cpp") << QStringLiteral("main.cpp");
        QTest::newRow("hpp") << QStringLiteral("main.hpp");
        QTest::newRow("py") << QStringLiteral("script.py");
        QTest::newRow("sh") << QStringLiteral("run.sh");
        QTest::newRow("lua") << QStringLiteral("init.lua");
        QTest::newRow("js") << QStringLiteral("app.js");
        QTest::newRow("ts") << QStringLiteral("app.ts");
        QTest::newRow("rs") << QStringLiteral("main.rs");
        QTest::newRow("uppercase") << QStringLiteral("README.TXT");
        QTest::newRow("mixed-case") << QStringLiteral("Notes.Md");
    }

    void textLoad()
    {
        QFETCH(QString, fileName);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(fileName);
        const QString contents =
            QStringLiteral("first line\n\tindented second line 日本語 😀\nthird line\n");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded = contents.toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();
        QVERIFY(isTextFile(path));

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QVERIFY(!result.request.isDirectory);

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(textView != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(textView->isReadOnly());
        QCOMPARE(textView->lineWrapMode(), QPlainTextEdit::NoWrap);
        QCOMPARE(textView->toPlainText(), contents);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("Text file")));
        QVERIFY(status->text().contains(QStringLiteral("3 lines")));

        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY2(window.width() <= 1024, "Text preview must respect the window width cap.");
        QVERIFY2(window.height() <= 768, "Text preview must respect the window height cap.");
    }

    void textBom()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("bom.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded =
            QByteArray("\xEF\xBB\xBF") + QStringLiteral("bom content\n").toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        QVERIFY(textView != nullptr);
        QCOMPARE(textView->toPlainText(), QStringLiteral("bom content\n"));
    }

    void textEmpty()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("empty.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(textView != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(textView->toPlainText().isEmpty());
        QVERIFY(status->text().contains(QStringLiteral("empty")));
    }

    void textInvalidUtf8_data()
    {
        QTest::addColumn<QByteArray>("contents");
        QTest::newRow("lone-continuation") << QByteArray("valid\x80invalid\n");
        QTest::newRow("truncated-sequence") << QByteArray("truncated\xE2\x82\n");
        QTest::newRow("overlong-encoding") << QByteArray("overlong\xC0\xAFslash\n");
        QTest::newRow("surrogate") << QByteArray("surrogate\xED\xA0\x80here\n");
    }

    void textInvalidUtf8()
    {
        QFETCH(QByteArray, contents);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("UTF-8")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void textNulByte()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("binary.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("hello\x00world\n", 12);
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("binary")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void textTooLarge_data()
    {
        QTest::addColumn<qint64>("size");
        QTest::addColumn<bool>("expectPreview");
        QTest::newRow("over-limit") << (kMaxTextFileBytes + 1) << false;
        QTest::newRow("at-limit") << kMaxTextFileBytes << true;
    }

    void textTooLarge()
    {
        QFETCH(qint64, size);
        QFETCH(bool, expectPreview);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("big.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        // Valid UTF-8 filler so only the size gate decides the outcome.
        QByteArray contents(size > 0 ? static_cast<int>(size - 1) : 0, 'a');
        contents += '\n';
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QCOMPARE(window.isTextPreview(), expectPreview);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        if (expectPreview) {
            auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
            QVERIFY(textView != nullptr);
            QCOMPARE(textView->toPlainText(), QString::fromUtf8(contents));
        } else {
            QVERIFY(!window.isImagePreview());
            QVERIFY(status->text().contains(QStringLiteral("too large")));
            window.show();
            QCoreApplication::processEvents();
            QVERIFY(window.isVisible());
        }
    }

    void textUnreadable()
    {
#ifdef Q_OS_LINUX
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("secret.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("top secret\n");
        QVERIFY(file.write(contents) == contents.size());
        file.close();
        QVERIFY(QFile::setPermissions(path, QFileDevice::Permissions()));
        if (QFileInfo(path).isReadable()) {
            QSKIP("Unreadable fixtures need a non-privileged user.");
        }

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isTextPreview());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("unreadable")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
#else
        QSKIP("Unreadable fixtures use Linux permission semantics.");
#endif
    }

    void textLongLines()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("wide.txt"));
        const QString contents(QString(10000, QLatin1Char('x')));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded = contents.toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        QVERIFY(textView != nullptr);
        QCOMPARE(textView->lineWrapMode(), QPlainTextEdit::NoWrap);
        QCOMPARE(textView->toPlainText(), contents);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY2(window.width() <= 1024, "A long unbroken line must not force a wide window.");
        QVERIFY(window.isVisible());
    }

    void textMultilineEndings()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("endings.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("dos\r\nline\nmac\rstyle\n");
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        QVERIFY(textView != nullptr);
        const QString shown = textView->toPlainText();
        for (const auto *word : {"dos", "line", "mac", "style"}) {
            QVERIFY2(shown.contains(QLatin1String(word)), word);
        }
        QVERIFY(!shown.contains(QLatin1Char('\r')));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void textEscapeClosesWindow()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("escape.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("escape me\n");
        QVERIFY(file.write(contents) == contents.size());
        file.close();
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(window.isVisible());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
    }

    void pdfInvalidFallback()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not a PDF document at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        // Without PDF magic this never reaches a renderer, in any build: no
        // crash, no image, no text, and a non-empty plain-text message.
        PreviewWindow window(result.request);
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(!status->text().isEmpty());
        QVERIFY(status->text().contains(QStringLiteral("preview")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

#ifdef PEEK_WITH_PDF
    void pdfFirstPageRegression()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("minimal.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray pdf = buildMinimalPdf();
        QVERIFY(file.write(pdf) == pdf.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.isImagePreview());
        auto *image = window.findChild<QLabel *>(QStringLiteral("imageLabel"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(image != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("PDF document")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("imageLabel"))->pixmap().isNull());
    }
#endif

    void fittedSizeAspect()
    {
        QCOMPARE(PreviewWindow::fittedSize(QSize(2000, 1000), QSize(1024, 768)), QSize(1024, 512));
        QCOMPARE(PreviewWindow::fittedSize(QSize(1000, 2000), QSize(1024, 768)), QSize(384, 768));
        QCOMPARE(PreviewWindow::fittedSize(QSize(100, 100), QSize(1024, 768)), QSize(100, 100));
        QCOMPARE(PreviewWindow::fittedSize(QSize(), QSize(1024, 768)), QSize());
        QCOMPARE(PreviewWindow::fittedSize(QSize(64, 48), QSize()), QSize(64, 48));
        // Aspect ratio is preserved for a 3:2 image.
        const QSize fitted = PreviewWindow::fittedSize(QSize(3000, 2000), QSize(2048, 2048));
        QCOMPARE(fitted, QSize(2048, 1365));
        QVERIFY(qAbs(fitted.width() * 2 - fitted.height() * 3) <= 2);
    }

    void largeImageDecodeBound()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("large.png"));
        QVERIFY(writeTestImage(path, QSize(3000, 2000), "png"));

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isImagePreview());
        const QSize decoded = window.originalImageSize();
        QVERIFY(decoded.width() <= 2048);
        QVERIFY(decoded.height() <= 2048);
        // 3:2 aspect preserved within rounding.
        QVERIFY(qAbs(decoded.width() * 2 - decoded.height() * 3) <= 4);
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.width() <= 1024);
        QVERIFY(window.height() <= 768);
    }

    void imageEscapeClosesWindow()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("escape.png"));
        QVERIFY(writeTestImage(path, QSize(64, 48), "png"));
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isImagePreview());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(window.isVisible());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
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
