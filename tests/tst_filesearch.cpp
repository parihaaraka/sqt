#include <QtTest>
#include "filesearch.h"
#include "textcodec.h"

/// The rules of the file search that do not need a gui: the file/directory
/// masks and the matcher (positions, options, snippets).
class TestFileSearch : public QObject
{
    Q_OBJECT

private:
    static FileSearchParams plain(const QString &text)
    {
        FileSearchParams p;
        p.text = text;
        return p;
    }

    /// findHits() through the parameters, the way the worker does it.
    static QVector<FileSearchHit> hits(const QString &text, const FileSearchParams &params)
    {
        QString err;
        const QRegularExpression re = FileSearch::buildPattern(params, &err);
        if (!err.isEmpty())
            return {};
        return FileSearch::findHits("f.sql", text, re, params.snippetLength, params.maxHits);
    }

private slots:

    // ---------------- encoding of a hit ----------------

    void hitCarriesEncoding()
    {
        // Whoever opens the file has to reproduce the very text the search read,
        // or the hit's line and column point elsewhere. Reading the encoding off
        // a file dialog instead is what once made "open in editor" do nothing at
        // all: the dialog's combo is empty until it has been shown, and an empty
        // encoding is refused outright.
        FileSearchParams params;
        FileSearch::FileText t = FileSearch::decode(QByteArray("select 1;\n"), params);
        QCOMPARE(t.encoding, QString("UTF-8"));
        QVERIFY(t.error.isEmpty());
        QVERIFY(!TextCodec::canonicalName(t.encoding).isEmpty());
    }

    // ---------------- masks ----------------

    void maskByExtension()
    {
        const auto set = FileSearch::MaskSet::fromString("*.sql");
        QVERIFY(set.matches("table.sql", "tree/table.sql"));
        QVERIFY(!set.matches("hl.conf", "hl.conf"));
        // the case of a file name is not the user's problem
        QVERIFY(set.matches("TABLE.SQL", "TABLE.SQL"));
    }

    void maskBareExtensionIsGlob()
    {
        // ".sql" is what one types; it must not be read as a literal name
        const auto set = FileSearch::MaskSet::fromString(".sql");
        QVERIFY(set.matches("table.sql", "table.sql"));
        QVERIFY(!set.matches("sql", "sql"));
    }

    void maskList()
    {
        const auto set = FileSearch::MaskSet::fromString("*.sql, *.qs ;*.conf");
        QVERIFY(set.matches("a.sql", "a.sql"));
        QVERIFY(set.matches("a.qs", "a.qs"));
        QVERIFY(set.matches("hl.conf", "hl.conf"));
        QVERIFY(!set.matches("a.cpp", "a.cpp"));
    }

    void maskEmptyMatchesNothingButIsEmpty()
    {
        const auto set = FileSearch::MaskSet::fromString("  ,; ");
        QVERIFY(set.isEmpty());
        QVERIFY(!set.matches("a.sql", "a.sql"));
    }

    void maskWithPathIsMatchedAgainstThePath()
    {
        const auto set = FileSearch::MaskSet::fromString("tree/*.sql");
        QVERIFY(set.matches("table.sql", "tree/table.sql"));
        // a name mask would have matched this one; a path mask must not
        QVERIFY(!set.matches("table.sql", "content/table.sql"));
        // '*' of a path mask does not cross a separator
        QVERIFY(!set.matches("table.sql", "tree/pg/table.sql"));
    }

    void maskWithDoubleStarCrossesDirectories()
    {
        const auto set = FileSearch::MaskSet::fromString("postgres/**/*.sql");
        QVERIFY(set.matches("table.sql", "postgres/tree/table.sql"));
        QVERIFY(set.matches("f4.sql", "postgres/f4.sql"));
        QVERIFY(!set.matches("f4.sql", "odbc/f4.sql"));
    }

    void maskOfDirectoryPrunesItsContent()
    {
        // what the exclusion field is for: a directory name excludes the subtree
        const auto set = FileSearch::MaskSet::fromString("build/");
        QVERIFY(set.matches("moc_x.cpp", "build/moc_x.cpp"));
        QVERIFY(set.matches("x.o", "build/cline/x.o"));
        QVERIFY(!set.matches("x.cpp", "src/x.cpp"));
    }

    void maskQuestionMarkAndClass()
    {
        const auto set = FileSearch::MaskSet::fromString("v?.sql,[ab]*.txt");
        QVERIFY(set.matches("v1.sql", "v1.sql"));
        QVERIFY(!set.matches("v10.sql", "v10.sql"));
        QVERIFY(set.matches("about.txt", "about.txt"));
        QVERIFY(!set.matches("cabout.txt", "cabout.txt"));
    }

    void maskDotsAreLiteral()
    {
        // a glob's '.' is a dot, not "any character"
        const auto set = FileSearch::MaskSet::fromString("a.b");
        QVERIFY(set.matches("a.b", "a.b"));
        QVERIFY(!set.matches("axb", "axb"));
    }

    // ---------------- matcher ----------------

    void findPlainPositions()
    {
        const QString text = "select 1;\nselect 2;\n  select 3;\n";
        const auto res = hits(text, plain("select"));
        QCOMPARE(res.size(), 3);
        QCOMPARE(res[0].line, 1);
        QCOMPARE(res[0].column, 1);
        QCOMPARE(res[0].position, 0);
        QCOMPARE(res[1].line, 2);
        QCOMPARE(res[1].column, 1);
        QCOMPARE(res[1].position, 10);
        QCOMPARE(res[2].line, 3);
        QCOMPARE(res[2].column, 3);
        QCOMPARE(res[2].length, 6);
    }

    void findIsCaseInsensitiveByDefault()
    {
        QCOMPARE(hits("SELECT x", plain("select")).size(), 1);

        FileSearchParams p = plain("select");
        p.caseSensitive = true;
        QCOMPARE(hits("SELECT x", p).size(), 0);
        QCOMPARE(hits("select x", p).size(), 1);
    }

    void findPlainTextIsNotARegexp()
    {
        // "a.c" must not match "abc" unless the regexp option is on
        QCOMPARE(hits("abc a.c", plain("a.c")).size(), 1);
        FileSearchParams p = plain("a.c");
        p.regexp = true;
        QCOMPARE(hits("abc a.c", p).size(), 2);
    }

    void findWholeWord()
    {
        FileSearchParams p = plain("id");
        p.wholeWord = true;
        const auto res = hits("id, oid, id_x, \"id\"", p);
        QCOMPARE(res.size(), 2);        // the bare id and the quoted one
        QCOMPARE(res[0].column, 1);
    }

    void findWholeWordWithNonWordEdges()
    {
        // \b before '(' would never match, so the boundary must be dropped there
        FileSearchParams p = plain("(x)");
        p.wholeWord = true;
        QCOMPARE(hits("f(x) + g(x)", p).size(), 2);
    }

    void wholeWordIsIgnoredForRegexp()
    {
        // The flag is not honoured around an expression, because no wrapper can
        // promise "one whole word" there - so the result must be exactly what the
        // expression alone matches. "foo|bar" is the case that reads as a
        // regression if the wrapper ever comes back: with it, either the "bar"
        // inside "foobar" matched (bare boundaries) or nothing did.
        FileSearchParams p = plain("foo|bar");
        p.regexp = true;
        p.wholeWord = true;

        FileSearchParams bare = plain("foo|bar");
        bare.regexp = true;
        QCOMPARE(hits("foobar", p).size(), hits("foobar", bare).size());
        QCOMPARE(hits("foo bar", p).size(), hits("foo bar", bare).size());
        QCOMPARE(hits("foobar", p).size(), 2);      // both halves, as written
    }

    void wholeWordDoesNotBreakRegexpsItCannotWrap()
    {
        // Patterns a wrapper measurably destroyed rather than narrowed. Each must
        // behave as if wholeWord were not set at all.
        struct { const char *pattern; const char *text; int expected; } cases[] = {
            // ends with its own \W: the right guard used to be evaluated where
            // that \W already sat, and the match was lost
            { "foo\\W",   "xfoo bar",  1 },
            // zero-width: no edges to guard
            { "(?=foo)",  "foo bar",   1 },
            // a backreference after a word character
            { "(a)\\1",   "xaa",       1 },
            // the author's own boundary must not be doubled
            { "\\bfoo\\b", "a foo b",  1 },
        };
        for (const auto &c: cases)
        {
            FileSearchParams p = plain(c.pattern);
            p.regexp = true;
            p.wholeWord = true;
            QCOMPARE(hits(c.text, p).size(), c.expected);
        }
    }

    void findRegexpMultiline()
    {
        // the file is matched as a whole, so a pattern may span lines
        FileSearchParams p = plain("create\\s+table");
        p.regexp = true;
        const auto res = hits("create\n  table t", p);
        QCOMPARE(res.size(), 1);
        QCOMPARE(res[0].line, 1);
        QCOMPARE(res[0].column, 1);
    }

    void findRegexpAnchorsPerLine()
    {
        // ^ and $ are per-line: that is what a multiline search is expected to do
        FileSearchParams p = plain("(?m)^select");
        p.regexp = true;
        QCOMPARE(hits("select 1;\nselect 2;\n", p).size(), 2);
    }

    void findInvalidRegexpReportsError()
    {
        FileSearchParams p = plain("(unbalanced");
        p.regexp = true;
        QString err;
        const QRegularExpression re = FileSearch::buildPattern(p, &err);
        QVERIFY(!re.isValid());
        QVERIFY(!err.isEmpty());
    }

    void findEmptyMatchDoesNotHang()
    {
        // "^" matches an empty string on every line: the walk must still advance
        FileSearchParams p = plain("^");
        p.regexp = true;
        p.maxHits = 10;
        const auto res = hits("a\nb\nc\n", p);
        QVERIFY(!res.isEmpty());
        QVERIFY(res.size() <= 10);
    }

    void findRespectsMaxHits()
    {
        FileSearchParams p = plain("a");
        p.maxHits = 3;
        QCOMPARE(hits("aaaaaaa", p).size(), 3);
    }

    void findUnicode()
    {
        FileSearchParams p = plain(QString::fromUtf8("\xd1\x82\xd0\xb0\xd0\xb1"));   // "tab" in cyrillic
        const QString text = QString::fromUtf8("-- \xd1\x82\xd0\xb0\xd0\xb1\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0\n");
        const auto res = hits(text, p);
        QCOMPARE(res.size(), 1);
        QCOMPARE(res[0].column, 4);
    }

    // ---------------- snippets ----------------

    void snippetKeepsTheMatchHighlighted()
    {
        const auto res = hits("  select from t\n", plain("from"));
        QCOMPARE(res.size(), 1);
        // the leading blanks are dropped, so the text starts at "select"
        QCOMPARE(res[0].snippet, QString("select from t"));
        QCOMPARE(res[0].snippet.mid(res[0].snippetOffset, res[0].snippetLength), QString("from"));
        QVERIFY(res[0].snippetTrimmed);
    }

    void snippetOfALongLineSlidesToTheMatch()
    {
        FileSearchParams p = plain("needle");
        p.snippetLength = 30;
        const QString text = QString(200, 'x') + "needle" + QString(200, 'y');
        const auto res = hits(text, p);
        QCOMPARE(res.size(), 1);
        QVERIFY(res[0].snippet.size() <= 30);
        // whatever the window, the match must be inside it
        QCOMPARE(res[0].snippet.mid(res[0].snippetOffset, res[0].snippetLength), QString("needle"));
    }

    void snippetStopsAtTheEndOfLine()
    {
        const auto res = hits("one\ntwo\nthree\n", plain("two"));
        QCOMPARE(res.size(), 1);
        QCOMPARE(res[0].snippet, QString("two"));
    }

    void multilineMatchSnippetIsClippedToItsFirstLine()
    {
        FileSearchParams p = plain("a\\s+b");
        p.regexp = true;
        const auto res = hits("xx a\n   b yy", p);
        QCOMPARE(res.size(), 1);
        QCOMPARE(res[0].snippet, QString("xx a"));
        // the hit itself spans the newline, the snippet shows what fits
        QVERIFY(res[0].length > res[0].snippetLength);
    }

    // ---------------- decoding ----------------

    void decodeNormalizesCrLf()
    {
        FileSearchParams p = plain("b");
        auto res = FileSearch::decode(QByteArray("a\r\nb\r\nc"), p);
        QVERIFY(res.error.isEmpty());
        // decode() itself does not normalize; the positions come out of the
        // normalized text, which is what readFile() hands over. Do the same here.
        res.text.replace("\r\n", "\n");
        const auto found = hits(res.text, p);
        QCOMPARE(found.size(), 1);
        QCOMPARE(found[0].line, 2);
        QCOMPARE(found[0].column, 1);
    }

    void decodeSkipsBinary()
    {
        FileSearchParams p = plain("x");
        const auto res = FileSearch::decode(QByteArray("PNG\0\0x", 6), p);
        QVERIFY(res.binary);
        QVERIFY(res.text.isEmpty());
    }

    void decodeBinaryAllowedWhenAsked()
    {
        FileSearchParams p = plain("x");
        p.skipBinary = false;
        const auto res = FileSearch::decode(QByteArray("a\0x", 3), p);
        QVERIFY(!res.binary);
    }

    void decodeFallsBackForSingleByteFiles()
    {
        FileSearchParams p = plain("x");
        p.fallbackEncoding = "windows-1251";
        // 0xf2 0xe0 0xe1 - "tab" in cp1251, not valid utf-8
        const QByteArray data = QByteArray::fromHex("f2e0e1");
        const auto res = FileSearch::decode(data, p);
        QVERIFY(res.error.isEmpty());
        QCOMPARE(res.encoding, QString("windows-1251"));
        QCOMPARE(res.text, QString::fromUtf8("\xd1\x82\xd0\xb0\xd0\xb1"));
    }

    void decodeUsesUtf8WhenItFits()
    {
        FileSearchParams p = plain("x");
        p.fallbackEncoding = "windows-1251";
        const QByteArray data = QString::fromUtf8("\xd1\x82\xd0\xb0\xd0\xb1").toUtf8();
        const auto res = FileSearch::decode(data, p);
        QCOMPARE(res.encoding, QString("UTF-8"));
        QCOMPARE(res.text, QString::fromUtf8("\xd1\x82\xd0\xb0\xd0\xb1"));
    }

    void decodeReadsBomMarkedUtf16()
    {
        // Every ascii character in utf-16 is a byte pair holding a NUL, so a
        // binary check applied to the raw bytes would reject the whole family -
        // and these are the files SSMS writes by default.
        FileSearchParams p = plain("select");
        QByteArray data = QByteArray("\xFF\xFE", 2);
        for (char c: QByteArray("select 1"))
        {
            data.append(c);
            data.append('\0');
        }
        const auto res = FileSearch::decode(data, p);
        QVERIFY(!res.binary);
        QCOMPARE(res.encoding, QString("UTF-16LE"));
        QCOMPARE(res.text, QString("select 1"));
        QCOMPARE(hits(res.text, p).size(), 1);
    }

    void decodeReadsUtf16WhenNamedExplicitly()
    {
        // Also without a BOM to go by: the encoding is named, so the binary
        // check must respect it.
        FileSearchParams p = plain("select");
        p.encoding = "UTF-16LE";
        QByteArray data;
        for (char c: QByteArray("select 1"))
        {
            data.append(c);
            data.append('\0');
        }
        const auto res = FileSearch::decode(data, p);
        QVERIFY(!res.binary);
        QCOMPARE(res.text, QString("select 1"));
    }

    void decodeStillSkipsBinaryNamedAsUtf16()
    {
        // The heuristic is not simply dropped for wide encodings: it applies to
        // the decoded text, where a real NUL *character* still means binary.
        FileSearchParams p = plain("x");
        p.encoding = "UTF-16LE";
        // one 'a', then a NUL code unit
        const QByteArray data = QByteArray("\x61\x00\x00\x00", 4);
        const auto res = FileSearch::decode(data, p);
        QVERIFY(res.binary);
        QVERIFY(res.text.isEmpty());
    }

    // ---------------- the walk ----------------

    void workerFindsHitsInATree()
    {
        // a small corpus of its own, inside the workspace
        QTemporaryDir dir(QDir::currentPath() + "/fsearchXXXXXX");
        QVERIFY(dir.isValid());
        const QString root = dir.path();

        auto write = [&root](const QString &relPath, const QByteArray &content) {
            const QString full = root + '/' + relPath;
            QDir().mkpath(QFileInfo(full).absolutePath());
            QFile f(full);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(content);
        };

        write("a.sql", "select needle from t;\n");
        write("sub/b.sql", "-- needle\nselect needle;\n");
        write("sub/c.txt", "needle in a text file\n");
        write("skipme/d.sql", "needle inside an excluded directory\n");
        write(".hidden/e.sql", "needle inside a hidden directory\n");

        FileSearchParams params = plain("needle");
        params.path = root;
        params.including = "*.sql";
        params.excluding = "skipme";

        FileSearchWorker worker;
        QVector<FileSearchHit> collected;
        FileSearchSummary summary;
        connect(&worker, &FileSearchWorker::batch, this,
                [&collected](quint64, QVector<FileSearchHit> hits) { collected += hits; });
        connect(&worker, &FileSearchWorker::finished, this,
                [&summary](quint64, FileSearchSummary s) { summary = s; });

        worker.search(params, 1);   // synchronous here, no thread involved

        QVERIFY(summary.error.isEmpty());
        QVERIFY(!summary.cancelled);
        QCOMPARE(summary.filesMatched, 2);      // a.sql and sub/b.sql
        QCOMPARE(collected.size(), 3);          // one hit + two hits
        QCOMPARE(summary.hits, 3);

        for (const auto &hit: collected)
        {
            QVERIFY(!hit.fileName.contains("skipme"));
            QVERIFY(!hit.fileName.contains(".hidden"));
            QVERIFY(hit.fileName.endsWith(".sql"));
        }
    }

    void workerSearchesTheEditorBuffer()
    {
        QTemporaryDir dir(QDir::currentPath() + "/fsearchXXXXXX");
        QVERIFY(dir.isValid());
        const QString fileName = dir.path() + "/a.sql";
        QFile f(fileName);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("nothing here\n");
        f.close();

        FileSearchParams params = plain("needle");
        params.path = dir.path();
        params.including = "*.sql";
        // an unsaved tab holds something else; that is what must be searched
        params.bufferTexts.insert(QFileInfo(fileName).absoluteFilePath(), "needle\n");

        FileSearchWorker worker;
        QVector<FileSearchHit> collected;
        connect(&worker, &FileSearchWorker::batch, this,
                [&collected](quint64, QVector<FileSearchHit> hits) { collected += hits; });
        worker.search(params, 1);

        QCOMPARE(collected.size(), 1);
        QCOMPARE(collected[0].line, 1);
    }

    void workerReportsAMissingDirectory()
    {
        FileSearchParams params = plain("x");
        params.path = QDir::currentPath() + "/no-such-directory-here";

        FileSearchWorker worker;
        FileSearchSummary summary;
        connect(&worker, &FileSearchWorker::finished, this,
                [&summary](quint64, FileSearchSummary s) { summary = s; });
        worker.search(params, 1);
        QVERIFY(!summary.error.isEmpty());
        QCOMPARE(summary.hits, 0);
    }

    void workerObeysCancellation()
    {
        QTemporaryDir dir(QDir::currentPath() + "/fsearchXXXXXX");
        QVERIFY(dir.isValid());
        for (int i = 0; i < 20; ++i)
        {
            QFile f(dir.path() + QString("/f%1.sql").arg(i));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("needle\n");
        }

        FileSearchParams params = plain("needle");
        params.path = dir.path();

        FileSearchWorker worker;
        FileSearchSummary summary;
        connect(&worker, &FileSearchWorker::finished, this,
                [&summary](quint64, FileSearchSummary s) { summary = s; });
        // cancelled before it even starts: the search must report nothing
        worker.cancelUpTo(5);
        worker.search(params, 5);
        QVERIFY(summary.cancelled);
        QCOMPARE(summary.hits, 0);

        // a later generation is unaffected by that cancellation
        summary = FileSearchSummary();
        worker.search(params, 6);
        QVERIFY(!summary.cancelled);
        QCOMPARE(summary.filesMatched, 20);
    }
};

QTEST_MAIN(TestFileSearch)
#include "tst_filesearch.moc"
