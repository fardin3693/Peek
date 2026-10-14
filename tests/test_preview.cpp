#include "audio_preview.h"
#include "image_provider.h"
#include "markdown_preview.h"
#include "markdown_provider.h"
#include "preview_request.h"
#include "preview_window.h"
#include "text_preview.h"
#include "text_provider.h"
#include "video_preview.h"

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
#include <QPushButton>
#include <QSlider>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>

#include <array>

#ifdef PEEK_WITH_MULTIMEDIA
#include "audio_provider.h"
#include <QMediaPlayer>
#endif

#ifdef PEEK_WITH_PDF
#include "pdf_provider.h"
#endif

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

// Assembles a minimal deterministic single-page PDF in memory: catalog,
// page tree, one Helvetica page, and a correct cross-reference table with
// computed offsets. No external producer or downloaded fixture needed.
// Pure Qt (no Poppler), so both PEEK_WITH_PDF and no-PDF builds use it.
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

    void textProviderSupports_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("supported");
        QTest::newRow("txt") << QStringLiteral("notes.txt") << true;
        QTest::newRow("uppercase-txt") << QStringLiteral("NOTES.TXT") << true;
        QTest::newRow("source") << QStringLiteral("main.cpp") << true;
        QTest::newRow("script") << QStringLiteral("run.py") << true;
        QTest::newRow("config") << QStringLiteral("data.json") << true;
        QTest::newRow("log") << QStringLiteral("server.LOG") << true;
        QTest::newRow("md-not-text") << QStringLiteral("notes.md") << false;
        QTest::newRow("markdown-not-text") << QStringLiteral("notes.markdown") << false;
        QTest::newRow("mixed-case-md-not-text") << QStringLiteral("Notes.Md") << false;
        QTest::newRow("png") << QStringLiteral("photo.png") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("no-extension") << QStringLiteral("notes") << false;
        QTest::newRow("text-suffix-not-final") << QStringLiteral("notes.txt.png") << false;
    }

    void textProviderSupports()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, supported);
        const auto expected = supported ? ProviderSupport::Yes : ProviderSupport::No;
        QCOMPARE(textProvider().supports(fileName), expected);
        QCOMPARE(::textProviderSupports(fileName), expected);
    }

    void textProviderRenderNullGuards()
    {
        // Null widgets must not crash: render refuses safely.
        QCOMPARE(textProvider().render(QStringLiteral("notes.txt"), nullptr, nullptr), false);
    }

    void textProviderRendersContent()
    {
        QCOMPARE(QLatin1String(textProvider().name), QLatin1String("text"));
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("provider.txt"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("provider renders\n");
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isTextPreview());
        auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
        QVERIFY(textView != nullptr);
        QVERIFY(textView->isReadOnly());
        QCOMPARE(textView->toPlainText(), QString::fromUtf8(contents));
    }

    void textProviderEmptyFileRenders()
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
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("empty")));
    }

    void textProviderFailureKeepsClaim()
    {
        // Invalid text stays claimed: status message, no text view, and no
        // fall-through to the image path.
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
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("Could not load a text preview")));
    }

    void textProviderLeavesOthersAlone()
    {
        // Non-text extensions never claim the text provider, so images and
        // unknown binaries still reach their existing paths.
        QCOMPARE(textProvider().supports(QStringLiteral("photo.png")), ProviderSupport::No);
        QCOMPARE(textProvider().supports(QStringLiteral("notes.bin")), ProviderSupport::No);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString imagePath = fixtures.filePath(QStringLiteral("tiny.png"));
        QVERIFY(writeTestImage(imagePath, QSize(8, 8), "PNG"));
        const auto imageResult = validatePreviewPath(imagePath);
        QVERIFY2(imageResult.error.isEmpty(), qPrintable(imageResult.error));
        {
            PreviewWindow window(imageResult.request);
            QVERIFY(window.isImagePreview());
            QVERIFY(!window.isTextPreview());
        }

        const QString unknownPath = fixtures.filePath(QStringLiteral("mystery.zzz"));
        QFile unknown(unknownPath);
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        const QByteArray garbage("not a supported file");
        QVERIFY(unknown.write(garbage) == garbage.size());
        unknown.close();
        const auto unknownResult = validatePreviewPath(unknownPath);
        QVERIFY2(unknownResult.error.isEmpty(), qPrintable(unknownResult.error));
        {
            PreviewWindow window(unknownResult.request);
            QVERIFY(!window.isTextPreview());
            QVERIFY(!window.isImagePreview());
            QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
            auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
            QVERIFY(status != nullptr);
            QCOMPARE(status->textFormat(), Qt::PlainText);
            QVERIFY(!status->text().isEmpty());
            QVERIFY(status->text().contains(QStringLiteral("image preview")));
        }
    }

    void markdownProviderSupports_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("supported");
        QTest::newRow("md") << QStringLiteral("notes.md") << true;
        QTest::newRow("markdown") << QStringLiteral("notes.markdown") << true;
        QTest::newRow("uppercase-md") << QStringLiteral("NOTES.MD") << true;
        QTest::newRow("uppercase-markdown") << QStringLiteral("NOTES.MARKDOWN") << true;
        QTest::newRow("mixed-case") << QStringLiteral("Notes.Md") << true;
        QTest::newRow("mixed-case-markdown") << QStringLiteral("Notes.MarkDown") << true;
        QTest::newRow("txt") << QStringLiteral("notes.txt") << false;
        QTest::newRow("png") << QStringLiteral("photo.png") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("no-extension") << QStringLiteral("notes") << false;
        QTest::newRow("md-suffix-not-final") << QStringLiteral("notes.md.txt") << false;
        QTest::newRow("txt-suffix-markdown") << QStringLiteral("notes.txt.md") << true;
    }

    void markdownProviderSupports()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, supported);
        const auto expected = supported ? ProviderSupport::Yes : ProviderSupport::No;
        QCOMPARE(markdownProvider().supports(fileName), expected);
        QCOMPARE(::markdownProviderSupports(fileName), expected);
        QCOMPARE(isMarkdownFile(fileName), supported);
    }

    void markdownProviderSupportsIsCheap()
    {
        // Identification is extension-only: missing files with a Markdown
        // suffix are still claimed, proving no content is read in supports().
        QCOMPARE(markdownProvider().supports(QStringLiteral("/nonexistent/notes.MD")),
                 ProviderSupport::Yes);
        QCOMPARE(markdownProvider().supports(QStringLiteral("/nonexistent/notes.txt")),
                 ProviderSupport::No);
    }

    void markdownProviderRenderNullGuards()
    {
        // Null widgets must not crash: render refuses safely.
        QCOMPARE(markdownProvider().render(QStringLiteral("notes.md"), nullptr, nullptr), false);
    }

    void markdownRendersFormattedContent()
    {
        QCOMPARE(QLatin1String(markdownProvider().name), QLatin1String("markdown"));
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("notes.md"));
        const QString contents =
            QStringLiteral("# Title\n\nA paragraph with **bold** and *italic* text.\n\n"
                           "- item one\n- item two\n\n"
                           "1. first\n2. second\n\n"
                           "Use `inline-code` here.\n\n"
                           "```cpp\nint answer = 42;\n```\n\n"
                           "> a wise quote\n\n"
                           "[example](https://example.com)\n\n"
                           "---\n\n"
                           "| head-a | head-b |\n"
                           "|---|---|\n"
                           "| cell-one | cell-two |\n");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded = contents.toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isMarkdownPreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *view = window.findChild<QTextBrowser *>(QStringLiteral("markdownView"));
        QVERIFY(view != nullptr);
        QVERIFY(view->isReadOnly());
        // Links stay inert: never follow clicks, never open externally.
        QVERIFY(!view->openLinks());
        QVERIFY(!view->openExternalLinks());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("Markdown file")));
        const QString shown = view->toPlainText();
        QVERIFY(shown.contains(QStringLiteral("Title")));
        QVERIFY(shown.contains(QStringLiteral("paragraph")));
        QVERIFY(shown.contains(QStringLiteral("bold")));
        QVERIFY(shown.contains(QStringLiteral("italic")));
        QVERIFY(shown.contains(QStringLiteral("item one")));
        QVERIFY(shown.contains(QStringLiteral("first")));
        QVERIFY(shown.contains(QStringLiteral("answer")));
        QVERIFY(shown.contains(QStringLiteral("inline-code")));
        QVERIFY(shown.contains(QStringLiteral("wise quote")));
        QVERIFY(shown.contains(QStringLiteral("example")));
        QVERIFY(shown.contains(QStringLiteral("cell-one")));
        QVERIFY(shown.contains(QStringLiteral("cell-two")));
        // Formatting markers are consumed by the renderer, not shown raw.
        QVERIFY(!shown.contains(QStringLiteral("**")));
        QVERIFY(!shown.contains(QStringLiteral("```")));
        QVERIFY(!shown.contains(QStringLiteral("`inline-code`")));
        // Horizontal rule and table survive as document structure, not text.
        QVERIFY(view->document()->toHtml().contains(QStringLiteral("<hr")));
        QVERIFY(view->document()->toHtml().contains(QStringLiteral("<table")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void markdownEmptyFileRenders()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("empty.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isMarkdownPreview());
        QVERIFY(!window.isTextPreview());
        auto *view = window.findChild<QTextBrowser *>(QStringLiteral("markdownView"));
        QVERIFY(view != nullptr);
        QVERIFY(view->toPlainText().isEmpty());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("empty")));
    }

    void markdownMalformedInputKeepsClaim()
    {
        // Non-UTF-8 Markdown stays claimed: status message, no Markdown
        // view, and no fall-through to text or image previews.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.markdown"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents("valid\x80invalid\n");
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isMarkdownPreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(window.findChild<QTextBrowser *>(QStringLiteral("markdownView")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("Could not load a Markdown preview")));
    }

    void markdownTooLargeKeepsClaim()
    {
        // Over-limit Markdown inherits the 2 MiB text cap and stays claimed.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("big.md"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray contents(static_cast<int>(kMaxTextFileBytes), 'a');
        contents += '\n';
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isMarkdownPreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isImagePreview());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QVERIFY(status->text().contains(QStringLiteral("too large")));
    }

    void markdownLeavesTextAndOthersAlone()
    {
        // The Markdown provider claims only Markdown: plain text still
        // renders as text, and dispatch disjointness holds both directions.
        QCOMPARE(markdownProvider().supports(QStringLiteral("notes.txt")), ProviderSupport::No);
        QCOMPARE(markdownProvider().supports(QStringLiteral("photo.png")), ProviderSupport::No);
        QCOMPARE(textProvider().supports(QStringLiteral("notes.md")), ProviderSupport::No);
        QCOMPARE(textProvider().supports(QStringLiteral("notes.markdown")), ProviderSupport::No);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("plain.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray textContents("# not a heading, just text\n");
        QVERIFY(textFile.write(textContents) == textContents.size());
        textFile.close();
        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        {
            PreviewWindow window(textResult.request);
            QVERIFY(window.isTextPreview());
            QVERIFY(!window.isMarkdownPreview());
            auto *textView = window.findChild<QPlainTextEdit *>(QStringLiteral("textView"));
            QVERIFY(textView != nullptr);
            // Plain text keeps its raw markers verbatim.
            QVERIFY(textView->toPlainText().contains(QStringLiteral("# not a heading")));
        }

        const QString mdPath = fixtures.filePath(QStringLiteral("doc.md"));
        QFile mdFile(mdPath);
        QVERIFY(mdFile.open(QIODevice::WriteOnly));
        const QByteArray mdContents("# real heading\n");
        QVERIFY(mdFile.write(mdContents) == mdContents.size());
        mdFile.close();
        const auto mdResult = validatePreviewPath(mdPath);
        QVERIFY2(mdResult.error.isEmpty(), qPrintable(mdResult.error));
        {
            PreviewWindow window(mdResult.request);
            QVERIFY(window.isMarkdownPreview());
            QVERIFY(!window.isTextPreview());
        }
    }

    void markdownEscapesRawHtml()
    {
        // Raw HTML in the source is shown escaped, never as live markup.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("xss.md"));
        const QString contents =
            QStringLiteral("# Hi\n\n<script>alert(1)</script>\n\n<b>not bold</b>\n");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded = contents.toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isMarkdownPreview());
        auto *view = window.findChild<QTextBrowser *>(QStringLiteral("markdownView"));
        QVERIFY(view != nullptr);
        QVERIFY(view->toPlainText().contains(QStringLiteral("<script>alert(1)</script>")));
        QVERIFY(view->toPlainText().contains(QStringLiteral("<b>not bold</b>")));
        QVERIFY(!view->document()->toHtml().contains(QStringLiteral("<script>")));
    }

    void markdownLinksStayInert()
    {
        // javascript: links render as text but carry no executable effect:
        // navigation is disabled and no external opener is attached.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("links.md"));
        const QString contents =
            QStringLiteral("[evil](javascript:alert(1))\n\n[ok](https://example.com)\n\n"
                           "![alt](https://example.com/x.png)\n");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray encoded = contents.toUtf8();
        QVERIFY(file.write(encoded) == encoded.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isMarkdownPreview());
        auto *view = window.findChild<QTextBrowser *>(QStringLiteral("markdownView"));
        QVERIFY(view != nullptr);
        QVERIFY(!view->openLinks());
        QVERIFY(!view->openExternalLinks());
        QVERIFY(view->searchPaths().isEmpty());
        const QString shown = view->toPlainText();
        QVERIFY(shown.contains(QStringLiteral("evil")));
        QVERIFY(shown.contains(QStringLiteral("ok")));
        // The remote image keeps its alt-text placeholder; the URL reference
        // in the document never resolves to fetched bytes because
        // loadResource() blocks non-local image loads (verified: the fetch
        // path returns empty without network access).
        QVERIFY(shown.contains(QChar(0xFFFC)) || shown.contains(QStringLiteral("alt")));
    }

    void imageProviderSupports_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("supported");
        QTest::newRow("png") << QStringLiteral("photo.png") << true;
        QTest::newRow("jpg") << QStringLiteral("photo.jpg") << true;
        QTest::newRow("jpeg") << QStringLiteral("photo.jpeg") << true;
        QTest::newRow("webp") << QStringLiteral("photo.webp") << true;
        QTest::newRow("bmp") << QStringLiteral("photo.bmp") << true;
        QTest::newRow("gif") << QStringLiteral("photo.gif") << true;
        QTest::newRow("uppercase-png") << QStringLiteral("PHOTO.PNG") << true;
        QTest::newRow("uppercase-jpg") << QStringLiteral("PHOTO.JPG") << true;
        QTest::newRow("mixed-case") << QStringLiteral("Photo.PnG") << true;
        QTest::newRow("mixed-case-jpeg") << QStringLiteral("Photo.JpEg") << true;
        QTest::newRow("txt") << QStringLiteral("notes.txt") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("no-extension") << QStringLiteral("photo") << false;
        QTest::newRow("image-suffix-not-final") << QStringLiteral("photo.png.txt") << false;
        QTest::newRow("text-suffix-image") << QStringLiteral("notes.txt.png") << true;
    }

    void imageProviderSupports()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, supported);
        const auto expected = supported ? ProviderSupport::Yes : ProviderSupport::No;
        QCOMPARE(imageProvider().supports(fileName), expected);
        QCOMPARE(::imageProviderSupports(fileName), expected);
    }

    void imageProviderSupportsIsCheap()
    {
        // Identification is extension-only: missing or unreadable files with
        // an image suffix are still claimed, proving no decode happens in
        // supports().
        QCOMPARE(imageProvider().supports(QStringLiteral("/nonexistent/photo.PNG")),
                 ProviderSupport::Yes);
        QCOMPARE(imageProvider().supports(QStringLiteral("/nonexistent/notes.txt")),
                 ProviderSupport::No);
        QCOMPARE(imageProvider().supports(QStringLiteral("photo.png.txt")), ProviderSupport::No);
    }

    void imageProviderRenderNullGuards()
    {
        // Null widgets must not crash: render refuses safely.
        QCOMPARE(imageProvider().render(QStringLiteral("photo.png"), nullptr, nullptr), false);
    }

    void imageProviderRendersContent()
    {
        QCOMPARE(QLatin1String(imageProvider().name), QLatin1String("image"));
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("provider.png"));
        QVERIFY(writeTestImage(path, QSize(64, 48), "png"));
        QCOMPARE(imageProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QCOMPARE(window.originalImageSize(), QSize(64, 48));
        auto *image = window.findChild<QLabel *>(QStringLiteral("imageLabel"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(image != nullptr);
        QVERIFY(status != nullptr);
        QCOMPARE(image->alignment(), Qt::AlignCenter);
        QVERIFY(image->minimumSize().width() >= 160);
        QVERIFY(image->minimumSize().height() >= 120);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("64 × 48")));
        QVERIFY(status->text().contains(QStringLiteral("px")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("imageLabel"))->pixmap().isNull());
    }

    void imageProviderFailureKeepsClaim()
    {
        // A recognized but corrupt image stays claimed: the image status
        // message stands, no view is created, and nothing falls through to
        // text or the generic path with a misleading message.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.png"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not image data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();
        QCOMPARE(imageProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("Could not load an image preview")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void imageProviderLeavesOthersAlone()
    {
        // Non-image extensions never claim the image provider, so text and
        // unknown binaries still reach their existing paths.
        QCOMPARE(imageProvider().supports(QStringLiteral("notes.txt")), ProviderSupport::No);
        QCOMPARE(imageProvider().supports(QStringLiteral("notes.bin")), ProviderSupport::No);
        QCOMPARE(imageProvider().supports(QStringLiteral("photo.png.txt")), ProviderSupport::No);
        QCOMPARE(imageProvider().supports(QStringLiteral("clip.mp4")), ProviderSupport::No);
        QCOMPARE(imageProvider().supports(QStringLiteral("sound.mp3")), ProviderSupport::No);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("notes.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray words("still text\n");
        QVERIFY(textFile.write(words) == words.size());
        textFile.close();
        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        {
            PreviewWindow window(textResult.request);
            QVERIFY(window.isTextPreview());
            QVERIFY(!window.isImagePreview());
        }

        const QString unknownPath = fixtures.filePath(QStringLiteral("mystery.zzz"));
        QFile unknown(unknownPath);
        QVERIFY(unknown.open(QIODevice::WriteOnly));
        const QByteArray garbage("not a supported file");
        QVERIFY(unknown.write(garbage) == garbage.size());
        unknown.close();
        QCOMPARE(imageProvider().supports(unknownPath), ProviderSupport::No);
        const auto unknownResult = validatePreviewPath(unknownPath);
        QVERIFY2(unknownResult.error.isEmpty(), qPrintable(unknownResult.error));
        {
            PreviewWindow window(unknownResult.request);
            QVERIFY(!window.isTextPreview());
            QVERIFY(!window.isImagePreview());
            auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
            QVERIFY(status != nullptr);
            QCOMPARE(status->text(),
                     QStringLiteral("No image preview is available for this file."));
        }
    }

    void videoExtension_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("isVideo");
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << true;
        QTest::newRow("mkv") << QStringLiteral("clip.mkv") << true;
        QTest::newRow("webm") << QStringLiteral("clip.webm") << true;
        QTest::newRow("mov") << QStringLiteral("clip.mov") << true;
        QTest::newRow("avi") << QStringLiteral("clip.avi") << true;
        QTest::newRow("m4v") << QStringLiteral("clip.m4v") << true;
        QTest::newRow("uppercase-mp4") << QStringLiteral("CLIP.MP4") << true;
        QTest::newRow("uppercase-mkv") << QStringLiteral("CLIP.MKV") << true;
        QTest::newRow("uppercase-webm") << QStringLiteral("CLIP.WEBM") << true;
        QTest::newRow("uppercase-mov") << QStringLiteral("CLIP.MOV") << true;
        QTest::newRow("uppercase-avi") << QStringLiteral("CLIP.AVI") << true;
        QTest::newRow("uppercase-m4v") << QStringLiteral("CLIP.M4V") << true;
        QTest::newRow("mixed-case") << QStringLiteral("Movie.MkV") << true;
        QTest::newRow("txt") << QStringLiteral("notes.txt") << false;
        QTest::newRow("png") << QStringLiteral("photo.png") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << false;
        QTest::newRow("video-suffix-not-final") << QStringLiteral("clip.mp4.txt") << false;
        QTest::newRow("no-extension") << QStringLiteral("clip") << false;
    }

    void videoExtension()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, isVideo);
        QCOMPARE(isVideoFile(fileName), isVideo);
    }

#ifdef PEEK_WITH_MULTIMEDIA
    void videoWidgetStartsPaused()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("clip.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isVideoPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());

        auto *player = window.findChild<QMediaPlayer *>();
        auto *playButton = window.findChild<QPushButton *>(QStringLiteral("playButton"));
        auto *seekSlider = window.findChild<QSlider *>(QStringLiteral("seekSlider"));
        auto *positionLabel = window.findChild<QLabel *>(QStringLiteral("positionLabel"));
        QVERIFY(player != nullptr);
        QVERIFY(playButton != nullptr);
        QVERIFY(seekSlider != nullptr);
        QVERIFY(positionLabel != nullptr);
        // Starts paused: constructed but never told to play.
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QCOMPARE(playButton->text(), QStringLiteral("Play"));

        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QVERIFY2(window.width() <= 1024, "Video preview must respect the window width cap.");
        QVERIFY2(window.height() <= 768, "Video preview must respect the window height cap.");
        // Safe cleanup: closing stops the player and the window destroys its
        // children without crashing.
        QVERIFY(window.close());
    }

    void videoInvalidFile()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not video data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        // Invalid media stays on the video path with a message: no crash, no
        // image, no text view.
        QVERIFY(window.isVideoPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("video"), Qt::CaseInsensitive));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
    }

    void videoRealFileSurvivesPlayback()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            QSKIP("ffmpeg is not on PATH; cannot synthesize a tiny video fixture.");
        }
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("tiny.mp4"));
        QProcess generator;
        generator.setProgram(ffmpeg);
        generator.setArguments({QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                QStringLiteral("testsrc=size=64x48:rate=10:duration=1"),
                                QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                                QStringLiteral("-c:v"), QStringLiteral("mpeg4"), path});
        generator.start();
        if (!generator.waitForFinished(15000) || generator.exitCode() != 0 ||
            !QFileInfo(path).isFile() || QFileInfo(path).size() == 0) {
            QSKIP("ffmpeg could not synthesize the tiny video fixture here.");
        }

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isVideoPreview());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(player != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        window.show();
        QCoreApplication::processEvents();
        // Give the backend a bounded moment to report a duration or an error;
        // either outcome is acceptable, the window must simply survive.
        for (int i = 0; i < 60; ++i) {
            QCoreApplication::processEvents();
            if (player->duration() > 0 || status->text().contains(QStringLiteral("Could not"))) {
                break;
            }
            QTest::qWait(50);
        }
        QVERIFY(window.isVisible());
        QVERIFY(window.isVideoPreview());
        QVERIFY(status->text().contains(QStringLiteral("video"), Qt::CaseInsensitive));
    }

    void videoEscapeClosesWindow()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("escape.mp4"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isVideoPreview());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(window.isVisible());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
    }

    void audioExtension_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("isAudio");
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << true;
        QTest::newRow("wav") << QStringLiteral("sound.wav") << true;
        QTest::newRow("flac") << QStringLiteral("sound.flac") << true;
        QTest::newRow("ogg") << QStringLiteral("sound.ogg") << true;
        QTest::newRow("opus") << QStringLiteral("sound.opus") << true;
        QTest::newRow("m4a") << QStringLiteral("sound.m4a") << true;
        QTest::newRow("aac") << QStringLiteral("sound.aac") << true;
        QTest::newRow("wma") << QStringLiteral("sound.wma") << true;
        QTest::newRow("uppercase-mp3") << QStringLiteral("SOUND.MP3") << true;
        QTest::newRow("uppercase-wav") << QStringLiteral("SOUND.WAV") << true;
        QTest::newRow("uppercase-flac") << QStringLiteral("SOUND.FLAC") << true;
        QTest::newRow("uppercase-ogg") << QStringLiteral("SOUND.OGG") << true;
        QTest::newRow("uppercase-opus") << QStringLiteral("SOUND.OPUS") << true;
        QTest::newRow("uppercase-m4a") << QStringLiteral("SOUND.M4A") << true;
        QTest::newRow("uppercase-aac") << QStringLiteral("SOUND.AAC") << true;
        QTest::newRow("uppercase-wma") << QStringLiteral("SOUND.WMA") << true;
        QTest::newRow("mixed-case") << QStringLiteral("Sound.FlAc") << true;
        QTest::newRow("txt") << QStringLiteral("notes.txt") << false;
        QTest::newRow("png") << QStringLiteral("photo.png") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false;
        QTest::newRow("m4v") << QStringLiteral("clip.m4v") << false;
        QTest::newRow("audio-suffix-not-final") << QStringLiteral("sound.mp3.txt") << false;
        QTest::newRow("no-extension") << QStringLiteral("sound") << false;
    }

    void audioExtension()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, isAudio);
        QCOMPARE(isAudioFile(fileName), isAudio);
    }

    void audioWidgetStartsPaused()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("clip.wav"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isAudioPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isVideoPreview());

        auto *player = window.findChild<QMediaPlayer *>();
        auto *playButton = window.findChild<QPushButton *>(QStringLiteral("playButton"));
        auto *seekSlider = window.findChild<QSlider *>(QStringLiteral("seekSlider"));
        auto *positionLabel = window.findChild<QLabel *>(QStringLiteral("positionLabel"));
        auto *audioLabel = window.findChild<QLabel *>(QStringLiteral("audioLabel"));
        QVERIFY(player != nullptr);
        QVERIFY(playButton != nullptr);
        QVERIFY(seekSlider != nullptr);
        QVERIFY(positionLabel != nullptr);
        QVERIFY(audioLabel != nullptr);
        // Starts paused: constructed but never told to play.
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QCOMPARE(playButton->text(), QStringLiteral("Play"));
        QVERIFY(window.windowTitle().contains(QStringLiteral("clip.wav")));

        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QVERIFY2(window.width() <= 1024, "Audio preview must respect the window width cap.");
        QVERIFY2(window.height() <= 768, "Audio preview must respect the window height cap.");
        // Safe cleanup: closing stops the player and the window destroys its
        // children without crashing.
        QVERIFY(window.close());
    }

    void audioInvalidFile()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.mp3"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not audio data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        // Invalid media stays on the audio path with a message: no crash, no
        // image, no text, no video widget.
        QVERIFY(window.isAudioPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isVideoPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("audio"), Qt::CaseInsensitive));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
    }

    void audioRealFileSurvivesPlayback()
    {
        const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
        if (ffmpeg.isEmpty()) {
            QSKIP("ffmpeg is not on PATH; cannot synthesize a tiny audio fixture.");
        }
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("tiny.wav"));
        QProcess generator;
        generator.setProgram(ffmpeg);
        generator.setArguments({QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                                QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                                QStringLiteral("sine=frequency=440:duration=1"),
                                QStringLiteral("-c:a"), QStringLiteral("pcm_s16le"), path});
        generator.start();
        if (!generator.waitForFinished(15000) || generator.exitCode() != 0 ||
            !QFileInfo(path).isFile() || QFileInfo(path).size() == 0) {
            QSKIP("ffmpeg could not synthesize the tiny audio fixture here.");
        }

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isAudioPreview());
        auto *player = window.findChild<QMediaPlayer *>();
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(player != nullptr);
        QVERIFY(status != nullptr);
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        window.show();
        QCoreApplication::processEvents();
        // Give the backend a bounded moment to report a duration or an error;
        // either outcome is acceptable, the window must simply survive.
        for (int i = 0; i < 60; ++i) {
            QCoreApplication::processEvents();
            if (player->duration() > 0 || status->text().contains(QStringLiteral("Could not"))) {
                break;
            }
            QTest::qWait(50);
        }
        QVERIFY(window.isVisible());
        QVERIFY(window.isAudioPreview());
        QVERIFY(status->text().contains(QStringLiteral("audio"), Qt::CaseInsensitive));
    }

    void audioEscapeClosesWindow()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("escape.ogg"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isAudioPreview());
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window, 3000));
        QVERIFY(window.isVisible());
        QTest::keyClick(&window, Qt::Key_Escape);
        QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 2000);
    }

    void audioVideoRepeatedPreviews()
    {
        // Audio and video players are independent instances: opening and
        // closing one must not disturb the other, in either order.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString audioPath = fixtures.filePath(QStringLiteral("clip.wav"));
        const QString videoPath = fixtures.filePath(QStringLiteral("clip.mp4"));
        for (const QString &path : {audioPath, videoPath}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
        }

        const auto audioResult = validatePreviewPath(audioPath);
        QVERIFY2(audioResult.error.isEmpty(), qPrintable(audioResult.error));
        PreviewWindow firstAudio(audioResult.request);
        QVERIFY(firstAudio.isAudioPreview());
        QVERIFY(!firstAudio.isVideoPreview());
        firstAudio.show();
        QCoreApplication::processEvents();
        QVERIFY(firstAudio.isVisible());
        QVERIFY(firstAudio.close());

        const auto videoResult = validatePreviewPath(videoPath);
        QVERIFY2(videoResult.error.isEmpty(), qPrintable(videoResult.error));
        PreviewWindow video(videoResult.request);
        QVERIFY(video.isVideoPreview());
        QVERIFY(!video.isAudioPreview());
        video.show();
        QCoreApplication::processEvents();
        QVERIFY(video.isVisible());
        QVERIFY(video.close());

        PreviewWindow secondAudio(audioResult.request);
        QVERIFY(secondAudio.isAudioPreview());
        QVERIFY(!secondAudio.isVideoPreview());
        secondAudio.show();
        QCoreApplication::processEvents();
        QVERIFY(secondAudio.isVisible());
        QVERIFY(secondAudio.close());
    }

    void audioProviderSupports_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("supported");
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << true;
        QTest::newRow("wav") << QStringLiteral("sound.wav") << true;
        QTest::newRow("flac") << QStringLiteral("sound.flac") << true;
        QTest::newRow("ogg") << QStringLiteral("sound.ogg") << true;
        QTest::newRow("opus") << QStringLiteral("sound.opus") << true;
        QTest::newRow("m4a") << QStringLiteral("sound.m4a") << true;
        QTest::newRow("aac") << QStringLiteral("sound.aac") << true;
        QTest::newRow("wma") << QStringLiteral("sound.wma") << true;
        QTest::newRow("uppercase-mp3") << QStringLiteral("SOUND.MP3") << true;
        QTest::newRow("uppercase-wav") << QStringLiteral("SOUND.WAV") << true;
        QTest::newRow("uppercase-flac") << QStringLiteral("SOUND.FLAC") << true;
        QTest::newRow("uppercase-ogg") << QStringLiteral("SOUND.OGG") << true;
        QTest::newRow("uppercase-opus") << QStringLiteral("SOUND.OPUS") << true;
        QTest::newRow("uppercase-m4a") << QStringLiteral("SOUND.M4A") << true;
        QTest::newRow("uppercase-aac") << QStringLiteral("SOUND.AAC") << true;
        QTest::newRow("uppercase-wma") << QStringLiteral("SOUND.WMA") << true;
        QTest::newRow("mixed-case") << QStringLiteral("Sound.FlAc") << true;
        QTest::newRow("mixed-case-m4a") << QStringLiteral("Sound.M4A") << true;
        QTest::newRow("txt") << QStringLiteral("notes.txt") << false;
        QTest::newRow("png") << QStringLiteral("photo.png") << false;
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false;
        QTest::newRow("m4v") << QStringLiteral("clip.m4v") << false;
        QTest::newRow("mkv") << QStringLiteral("clip.mkv") << false;
        QTest::newRow("audio-suffix-not-final") << QStringLiteral("sound.mp3.txt") << false;
        QTest::newRow("no-extension") << QStringLiteral("sound") << false;
    }

    void audioProviderSupports()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, supported);
        const auto expected = supported ? ProviderSupport::Yes : ProviderSupport::No;
        QCOMPARE(audioProvider().supports(fileName), expected);
        QCOMPARE(::audioProviderSupports(fileName), expected);
    }

    void audioProviderSupportsIsCheap()
    {
        // Identification is extension-only: missing or unreadable files with
        // an audio suffix are still claimed, proving no player is opened and
        // no audio is decoded in supports().
        QCOMPARE(audioProvider().supports(QStringLiteral("/nonexistent/sound.MP3")),
                 ProviderSupport::Yes);
        QCOMPARE(audioProvider().supports(QStringLiteral("/nonexistent/notes.txt")),
                 ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("sound.mp3.txt")), ProviderSupport::No);
        // Audio/video extension sets are disjoint (m4a vs m4v).
        QCOMPARE(audioProvider().supports(QStringLiteral("clip.m4v")), ProviderSupport::No);
        QVERIFY(!isVideoFile(QStringLiteral("sound.m4a")));
    }

    void audioProviderRenderNullGuards()
    {
        // Null widgets must not crash: render refuses safely.
        QCOMPARE(audioProvider().render(QStringLiteral("sound.mp3"), nullptr, nullptr), false);
    }

    void audioProviderRendersContent()
    {
        QCOMPARE(QLatin1String(audioProvider().name), QLatin1String("audio"));
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("provider.wav"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(audioProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isAudioPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isVideoPreview());

        auto *player = window.findChild<QMediaPlayer *>();
        auto *playButton = window.findChild<QPushButton *>(QStringLiteral("playButton"));
        auto *seekSlider = window.findChild<QSlider *>(QStringLiteral("seekSlider"));
        auto *positionLabel = window.findChild<QLabel *>(QStringLiteral("positionLabel"));
        auto *audioLabel = window.findChild<QLabel *>(QStringLiteral("audioLabel"));
        auto *controls = window.findChild<QWidget *>(QStringLiteral("audioControls"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(player != nullptr);
        QVERIFY(playButton != nullptr);
        QVERIFY(seekSlider != nullptr);
        QVERIFY(positionLabel != nullptr);
        QVERIFY(audioLabel != nullptr);
        QVERIFY(controls != nullptr);
        QVERIFY(status != nullptr);
        // Initial playback state: paused, never told to play.
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QCOMPARE(playButton->text(), QStringLiteral("Play"));
        QCOMPARE(seekSlider->minimum(), 0);
        QCOMPARE(seekSlider->maximum(), 0);
        QCOMPARE(positionLabel->text(), QStringLiteral("0:00 / 0:00"));
        QCOMPARE(positionLabel->textFormat(), Qt::PlainText);
        QCOMPARE(audioLabel->text(), QStringLiteral("Audio preview"));
        QCOMPARE(audioLabel->textFormat(), Qt::PlainText);
        QCOMPARE(audioLabel->alignment(), Qt::AlignCenter);
        QVERIFY(audioLabel->minimumSize().width() >= 160);
        QVERIFY(audioLabel->minimumSize().height() >= 120);
        QCOMPARE(status->text(), QStringLiteral("Audio preview — paused."));
        QCOMPARE(status->textFormat(), Qt::PlainText);

        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
        QVERIFY2(window.width() <= 1024, "Audio preview must respect the window width cap.");
        QVERIFY2(window.height() <= 768, "Audio preview must respect the window height cap.");
        QVERIFY(window.close());
    }

    void audioProviderFailureKeepsClaim()
    {
        // A recognized but unplayable audio file stays claimed: the audio
        // status line stands, controls stay in place, and nothing falls
        // through to image, text, video, or the generic fallback.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("broken.mp3"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not audio data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();
        QCOMPARE(audioProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isAudioPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(!window.isVideoPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        QVERIFY(window.findChild<QWidget *>(QStringLiteral("videoControls")) == nullptr);
        // Controls stay in place so the failure never swaps in another preview.
        QVERIFY(window.findChild<QPushButton *>(QStringLiteral("playButton")) != nullptr);
        QVERIFY(window.findChild<QSlider *>(QStringLiteral("seekSlider")) != nullptr);
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("audioLabel")) != nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("audio"), Qt::CaseInsensitive));
        QVERIFY(status->text() != QStringLiteral("No image preview is available for this file."));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
    }

    void audioProviderLeavesOthersAlone()
    {
        // Non-audio extensions never claim the audio provider, so text,
        // images, video, and unknown binaries still reach their paths.
        QCOMPARE(audioProvider().supports(QStringLiteral("notes.txt")), ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("photo.png")), ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("clip.mp4")), ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("clip.m4v")), ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("notes.bin")), ProviderSupport::No);
        QCOMPARE(audioProvider().supports(QStringLiteral("sound.mp3.txt")), ProviderSupport::No);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("notes.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray words("still text\n");
        QVERIFY(textFile.write(words) == words.size());
        textFile.close();
        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        {
            PreviewWindow window(textResult.request);
            QVERIFY(window.isTextPreview());
            QVERIFY(!window.isAudioPreview());
        }

        const QString imagePath = fixtures.filePath(QStringLiteral("photo.png"));
        QVERIFY(writeTestImage(imagePath, QSize(64, 48), "png"));
        const auto imageResult = validatePreviewPath(imagePath);
        QVERIFY2(imageResult.error.isEmpty(), qPrintable(imageResult.error));
        {
            PreviewWindow window(imageResult.request);
            QVERIFY(window.isImagePreview());
            QVERIFY(!window.isAudioPreview());
        }
    }

    void audioRepeatedPreviews()
    {
        // Repeated open/close of the same audio file is safe: each window
        // owns an independent player that stops on close/destruction.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("repeat.wav"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        for (int i = 0; i < 2; ++i) {
            PreviewWindow window(result.request);
            QVERIFY(window.isAudioPreview());
            auto *player = window.findChild<QMediaPlayer *>();
            QVERIFY(player != nullptr);
            QVERIFY(player->playbackState() != QMediaPlayer::PlayingState);
            window.show();
            QCoreApplication::processEvents();
            QVERIFY(window.isVisible());
            QVERIFY(window.close());
        }
    }
#endif

    void audioLeavesOtherPreviews()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("notes.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray words("still text\n");
        QVERIFY(textFile.write(words) == words.size());
        textFile.close();
        const QString imagePath = fixtures.filePath(QStringLiteral("photo.png"));
        QVERIFY(writeTestImage(imagePath, QSize(64, 48), "png"));

        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        PreviewWindow textWindow(textResult.request);
        QVERIFY(textWindow.isTextPreview());
#ifdef PEEK_WITH_MULTIMEDIA
        QVERIFY(!textWindow.isAudioPreview());
#endif

        const auto imageResult = validatePreviewPath(imagePath);
        QVERIFY2(imageResult.error.isEmpty(), qPrintable(imageResult.error));
        PreviewWindow imageWindow(imageResult.request);
        QVERIFY(imageWindow.isImagePreview());
#ifdef PEEK_WITH_MULTIMEDIA
        QVERIFY(!imageWindow.isAudioPreview());
#endif

        // Audio extensions never resolve as text, image, or video fixtures
        // and vice versa.
        QVERIFY(!isAudioFile(textPath));
        QVERIFY(!isAudioFile(imagePath));
        QVERIFY(!isAudioFile(fixtures.filePath(QStringLiteral("clip.mp4"))));
        QVERIFY(!isVideoFile(fixtures.filePath(QStringLiteral("sound.mp3"))));
        QVERIFY(!isTextFile(fixtures.filePath(QStringLiteral("sound.wav"))));
    }

#ifndef PEEK_WITH_MULTIMEDIA
    void audioDisabledReportsUnsupported()
    {
        // Without the Multimedia backend audio extensions are still
        // recognized (isAudioFile is backend-independent) but report as
        // unsupported instead of reaching the image path with a misleading
        // message.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("sound.MP3"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("this is not audio data at all");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();
        QVERIFY(isAudioFile(path));

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isAudioPreview());
        QVERIFY(!window.isVideoPreview());
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QCOMPARE(status->text(), QStringLiteral("No audio preview is available for this file "
                                                "(Qt Multimedia is not installed)."));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
        QVERIFY(window.close());
    }
#endif

    void videoLeavesOtherPreviews()
    {
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("notes.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray words("still text\n");
        QVERIFY(textFile.write(words) == words.size());
        textFile.close();
        const QString imagePath = fixtures.filePath(QStringLiteral("photo.png"));
        QVERIFY(writeTestImage(imagePath, QSize(64, 48), "png"));

        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        PreviewWindow textWindow(textResult.request);
        QVERIFY(textWindow.isTextPreview());
        QVERIFY(!textWindow.isVideoPreview());

        const auto imageResult = validatePreviewPath(imagePath);
        QVERIFY2(imageResult.error.isEmpty(), qPrintable(imageResult.error));
        PreviewWindow imageWindow(imageResult.request);
        QVERIFY(imageWindow.isImagePreview());
        QVERIFY(!imageWindow.isVideoPreview());

        // Video extensions never resolve as text or image fixtures and vice
        // versa.
        QVERIFY(!isVideoFile(textPath));
        QVERIFY(!isVideoFile(imagePath));
        QVERIFY(!isTextFile(fixtures.filePath(QStringLiteral("clip.mp4"))));
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

#ifdef PEEK_WITH_PDF
    void pdfProviderSupports_data()
    {
        QTest::addColumn<QString>("fileName");
        QTest::addColumn<bool>("withMagic");
        QTest::addColumn<bool>("supported");
        QTest::newRow("pdf") << QStringLiteral("doc.pdf") << true << true;
        QTest::newRow("uppercase-pdf") << QStringLiteral("DOC.PDF") << true << true;
        QTest::newRow("mixed-case") << QStringLiteral("Doc.PdF") << true << true;
        QTest::newRow("no-magic") << QStringLiteral("broken.pdf") << false << false;
        QTest::newRow("empty") << QStringLiteral("empty.pdf") << false << false;
        QTest::newRow("magic-txt") << QStringLiteral("notes.txt") << true << false;
        QTest::newRow("magic-png") << QStringLiteral("photo.png") << true << false;
        QTest::newRow("mp3") << QStringLiteral("sound.mp3") << false << false;
        QTest::newRow("mp4") << QStringLiteral("clip.mp4") << false << false;
        QTest::newRow("bin") << QStringLiteral("notes.bin") << false << false;
        QTest::newRow("no-extension") << QStringLiteral("document") << true << false;
        QTest::newRow("pdf-suffix-not-final") << QStringLiteral("doc.pdf.txt") << true << false;
    }

    void pdfProviderSupports()
    {
        QFETCH(QString, fileName);
        QFETCH(bool, withMagic);
        QFETCH(bool, supported);
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(fileName);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray contents =
            withMagic ? buildMinimalPdf() : QByteArray("this is not a PDF document at all");
        QVERIFY(file.write(contents) == contents.size());
        file.close();

        const auto expected = supported ? ProviderSupport::Yes : ProviderSupport::No;
        QCOMPARE(pdfProvider().supports(path), expected);
        QCOMPARE(::pdfProviderSupports(path), expected);
    }

    void pdfProviderSupportsUnreadable()
    {
        // Identification never claims what it cannot read: missing files
        // report No instead of crashing or decoding.
        QCOMPARE(pdfProvider().supports(QStringLiteral("/nonexistent/missing.pdf")),
                 ProviderSupport::No);
        QCOMPARE(::pdfProviderSupports(QStringLiteral("/nonexistent/missing.pdf")),
                 ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("photo.png.txt")), ProviderSupport::No);
    }

    void pdfProviderRenderNullGuards()
    {
        // Null widgets must not crash: render refuses safely.
        QCOMPARE(pdfProvider().render(QStringLiteral("doc.pdf"), nullptr, nullptr), false);
    }

    void pdfProviderRendersContent()
    {
        QCOMPARE(QLatin1String(pdfProvider().name), QLatin1String("pdf"));
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("provider.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray pdf = buildMinimalPdf();
        QVERIFY(file.write(pdf) == pdf.size());
        file.close();
        QCOMPARE(pdfProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        auto *image = window.findChild<QLabel *>(QStringLiteral("imageLabel"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(image != nullptr);
        QVERIFY(status != nullptr);
        QCOMPARE(image->alignment(), Qt::AlignCenter);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("PDF document")));
        QVERIFY(status->text().contains(QStringLiteral("page 1 of 1")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<QLabel *>(QStringLiteral("imageLabel"))->pixmap().isNull());
    }

    void pdfProviderFailureKeepsClaim()
    {
        // A claimed but unrendersable PDF stays claimed: the PDF status
        // message stands, no view is created, and nothing falls through to
        // text, image, or the generic path with a misleading message.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("corrupt.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray garbage("%PDF-1.4\nthis is not a valid page tree at all\n");
        QVERIFY(file.write(garbage) == garbage.size());
        file.close();
        QCOMPARE(pdfProvider().supports(path), ProviderSupport::Yes);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QVERIFY(status->text().contains(QStringLiteral("Could not load a PDF preview")));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
    }

    void pdfProviderEmptyFileFallsThrough()
    {
        // An empty .pdf has no magic, so it is never claimed: the generic
        // fallback stands instead of a PDF error.
        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString path = fixtures.filePath(QStringLiteral("empty.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        QCOMPARE(pdfProvider().supports(path), ProviderSupport::No);

        const auto result = validatePreviewPath(path);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));

        PreviewWindow window(result.request);
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->text(), QStringLiteral("No image preview is available for this file."));
    }

    void pdfProviderLeavesOthersAlone()
    {
        // Non-PDF files never claim the PDF provider, so text, images, and
        // unknown binaries still reach their existing paths.
        QCOMPARE(pdfProvider().supports(QStringLiteral("notes.txt")), ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("photo.png")), ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("clip.mp4")), ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("sound.mp3")), ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("notes.bin")), ProviderSupport::No);
        QCOMPARE(pdfProvider().supports(QStringLiteral("doc.pdf.txt")), ProviderSupport::No);

        QTemporaryDir fixtures;
        QVERIFY(fixtures.isValid());
        const QString textPath = fixtures.filePath(QStringLiteral("notes.txt"));
        QFile textFile(textPath);
        QVERIFY(textFile.open(QIODevice::WriteOnly));
        const QByteArray words("still text\n");
        QVERIFY(textFile.write(words) == words.size());
        textFile.close();
        const auto textResult = validatePreviewPath(textPath);
        QVERIFY2(textResult.error.isEmpty(), qPrintable(textResult.error));
        {
            PreviewWindow window(textResult.request);
            QVERIFY(window.isTextPreview());
            QVERIFY(!window.isImagePreview());
        }

        const QString imagePath = fixtures.filePath(QStringLiteral("photo.png"));
        QVERIFY(writeTestImage(imagePath, QSize(64, 48), "png"));
        const auto imageResult = validatePreviewPath(imagePath);
        QVERIFY2(imageResult.error.isEmpty(), qPrintable(imageResult.error));
        {
            PreviewWindow window(imageResult.request);
            QVERIFY(window.isImagePreview());
            QVERIFY(!window.isTextPreview());
        }
    }
#endif

#ifndef PEEK_WITH_PDF
    void pdfDisabledFallsThrough()
    {
        // Without the Poppler backend a valid PDF is never claimed: it falls
        // through to the generic fallback instead of rendering or failing
        // with a PDF error.
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
        QVERIFY(!window.isImagePreview());
        QVERIFY(!window.isTextPreview());
        QVERIFY(window.findChild<QLabel *>(QStringLiteral("imageLabel")) == nullptr);
        QVERIFY(window.findChild<QPlainTextEdit *>(QStringLiteral("textView")) == nullptr);
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusLabel"));
        QVERIFY(status != nullptr);
        QCOMPARE(status->textFormat(), Qt::PlainText);
        QCOMPARE(status->text(), QStringLiteral("No image preview is available for this file."));
        window.show();
        QCoreApplication::processEvents();
        QVERIFY(window.isVisible());
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
