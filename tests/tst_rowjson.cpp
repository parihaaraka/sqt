#include <QtTest>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonArray>
#include <QAbstractTableModel>
#include "rowjson.h"

using namespace RowJson;

namespace
{

/// A stand-in for the resultset grid's model: four columns of the kinds that
/// matter (int, text, numeric, json) and two rows, one of which holds text that
/// merely looks like json.
class TestModel : public QAbstractTableModel
{
public:
    int rowCount(const QModelIndex & = QModelIndex()) const override { return 2; }
    int columnCount(const QModelIndex & = QModelIndex()) const override { return 4; }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::EditRole))
            return QVariant();

        const bool first = (index.row() == 0);
        switch (index.column())
        {
        case 0: return (first ? 1 : 2);
        case 1: return (first ? QString("first") : QString("second"));
        case 2: return (first ? QString("12345678901234567890.12345") : QString("0.10"));
        case 3: return (first ? jsonCellText() : QString("{not really json"));
        }
        return QVariant();
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override
    {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
            return QVariant();
        static const char *names[] = {"id", "name", "amount", "doc"};
        return (section >= 0 && section < 4 ? QString(names[section]) : QVariant());
    }

    /// The column holding json, and its content in row 0.
    int jsonColumn() const { return 3; }
    static QString jsonCellText() { return QStringLiteral("{\"a\": 1, \"nested\": {\"deep\": 7}}"); }

    /// The dbms type names, as AppEventHandler supplies them from the DataTable.
    TypeNameFn typeNameFn() const
    {
        return [](int column) -> QString {
            switch (column)
            {
            case 0: return "integer";
            case 1: return "text";
            case 2: return "numeric(30,5)";
            case 3: return "jsonb";
            }
            return QString();
        };
    }
};

} // namespace

/// The json Ctrl+J builds out of a grid selection. Every expectation below is
/// also checked for being valid json, since a document that does not parse is
/// worse than useless in a viewer that highlights it.
class TestRowJson : public QObject
{
    Q_OBJECT

private:
    static Cell c(const QString &name, const QVariant &value, Kind kind = Kind::Auto)
    {
        return Cell{name, value, kind};
    }

    /// Parses \a text, failing the test with the parser's own complaint.
    static QJsonDocument parsed(const QString &text)
    {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
        if (err.error != QJsonParseError::NoError)
        {
            QTest::qFail(qPrintable(QString("not valid json (%1) in:\n%2")
                                    .arg(err.errorString(), text)),
                         __FILE__, __LINE__);
        }
        return doc;
    }

private slots:
    /// The reason this is built as text: QJsonObject sorts its keys, and a row
    /// must read in the order the query selected its columns.
    void keepsColumnOrder()
    {
        const Row row{c("zebra", 1), c("alpha", 2), c("middle", 3)};
        const QString text = objectText(row);

        QVERIFY(text.indexOf("zebra") < text.indexOf("alpha"));
        QVERIFY(text.indexOf("alpha") < text.indexOf("middle"));
        // and it is still json
        QVERIFY(parsed(text).isObject());
    }

    void nullAndMissingValues()
    {
        const Row row{c("a", QVariant()), c("b", QVariant(QMetaType(QMetaType::QString)))};
        const QString text = objectText(row);
        const QJsonObject o = parsed(text).object();
        QVERIFY(o.value("a").isNull());
        QVERIFY(o.value("b").isNull());
    }

    void numbersStayNumbersTextStaysText()
    {
        const Row row{
            c("i", 42),
            c("i64", QVariant(qint64(9007199254740993LL))), // 2^53+1, exact in json
            c("d", 1.5),
            c("b", true),
            c("s", QString("42")),                          // a numeric-looking string
        };
        const QString text = objectText(row);
        const QJsonObject o = parsed(text).object();

        QVERIFY(o.value("i").isDouble());
        QCOMPARE(o.value("i").toInt(), 42);
        QCOMPARE(o.value("i64").toInteger(), qint64(9007199254740993LL));
        QVERIFY(o.value("d").isDouble());
        QVERIFY(o.value("b").isBool());
        // the point: a string that happens to hold digits is not turned into a number
        QVERIFY(o.value("s").isString());
        QCOMPARE(o.value("s").toString(), QString("42"));
    }

    /// numeric/decimal keeps every digit, which a json number (a double) would
    /// not - so it is rendered as a string on purpose.
    void numericIsAString()
    {
        const QString wide = "123456789012345678901234567890.123456789";
        const Row row{c("n", wide, Kind::Numeric), c("m", QString("0.10"), Kind::Numeric)};
        const QJsonObject o = parsed(objectText(row)).object();

        QVERIFY(o.value("n").isString());
        QCOMPARE(o.value("n").toString(), wide);
        // trailing zeros are part of what the server printed, and survive
        QCOMPARE(o.value("m").toString(), QString("0.10"));
    }

    void numericNullStaysNull()
    {
        const Row row{c("n", QVariant(), Kind::Numeric)};
        QVERIFY(parsed(objectText(row)).object().value("n").isNull());
    }

    /// A text column holding json is expanded into a nested node rather than
    /// shown as one long escaped line - the thing the user asked for.
    void jsonTextExpandsToNestedNode()
    {
        const Row row{c("payload", QString("{\"b\": 2, \"a\": [1, 2]}"))};
        const QString text = objectText(row);

        const QJsonObject o = parsed(text).object();
        QVERIFY2(o.value("payload").isObject(), "json in a text column must become a node");
        QCOMPARE(o.value("payload").toObject().value("b").toInt(), 2);
        // no escaped quotes: it is a real node, not a string
        QVERIFY(!text.contains("\\\""));
    }

    void jsonArrayInTextExpands()
    {
        const Row row{c("list", QString("[1, 2, {\"a\": 3}]"))};
        const QJsonObject o = parsed(objectText(row)).object();
        QVERIFY(o.value("list").isArray());
        QCOMPARE(o.value("list").toArray().size(), 3);
    }

    /// The risk the user pointed out: text that merely starts with '{' or '['
    /// must not break the document.
    void braceLeadingTextThatIsNotJsonStaysAString()
    {
        const QStringList notJson{
            "{not json}",
            "{",
            "[1, 2",
            "{\"a\": }",
            "[unclosed",
            "{a: 1}",              // unquoted key - not json
            "{\"a\": 1} trailing", // json followed by junk
        };

        for (const QString &v: notJson)
        {
            const Row row{c("x", v), c("after", 1)};
            const QString text = objectText(row);
            const QJsonDocument doc = parsed(text); // must still be valid json
            const QJsonObject o = doc.object();
            QVERIFY2(o.value("x").isString(),
                     qPrintable(QString("`%1` must stay a string").arg(v)));
            QCOMPARE(o.value("x").toString(), v);
            // the rest of the row survived
            QCOMPARE(o.value("after").toInt(), 1);
        }
    }

    /// A json/jsonb column is embedded whatever its shape - including the bare
    /// scalars QJsonDocument will not parse on its own.
    void jsonColumnEmbedsScalars()
    {
        const Row row{
            c("o", QString("{\"a\": 1}"), Kind::Json),
            c("a", QString("[1, 2]"), Kind::Json),
            c("n", QString("123"), Kind::Json),
            c("s", QString("\"text\""), Kind::Json),
            c("b", QString("true"), Kind::Json),
            c("z", QString("null"), Kind::Json),
        };
        const QJsonObject o = parsed(objectText(row)).object();

        QVERIFY(o.value("o").isObject());
        QVERIFY(o.value("a").isArray());
        QVERIFY(o.value("n").isDouble());
        QCOMPARE(o.value("n").toInt(), 123);
        QVERIFY(o.value("s").isString());
        QCOMPARE(o.value("s").toString(), QString("text"));
        QVERIFY(o.value("b").isBool());
        QVERIFY(o.value("z").isNull());
    }

    /// A json column may still hold something broken (a cast, a bad load); the
    /// document must survive it.
    void brokenJsonColumnFallsBackToText()
    {
        const Row row{c("j", QString("{oops"), Kind::Json), c("after", 1)};
        const QJsonObject o = parsed(objectText(row)).object();
        QVERIFY(o.value("j").isString());
        QCOMPARE(o.value("j").toString(), QString("{oops"));
        QCOMPARE(o.value("after").toInt(), 1);
    }

    /// Dates arrive as strings from the pg backend (it keeps the server's own
    /// text) and as Qt types from odbc, which are rendered as ISO-8601.
    void datesAreIsoAndUnlocalised()
    {
        const Row row{
            c("pg_ts", QString("2026-08-30 12:34:56.789123+03")), // pg: text, untouched
            c("odbc_d", QDate(2026, 8, 30)),
            c("odbc_t", QTime(12, 34, 56, 789)),
            c("odbc_ts", QDateTime(QDate(2026, 8, 30), QTime(12, 34, 56, 789))),
        };
        const QJsonObject o = parsed(objectText(row)).object();

        QCOMPARE(o.value("pg_ts").toString(), QString("2026-08-30 12:34:56.789123+03"));
        QCOMPARE(o.value("odbc_d").toString(), QString("2026-08-30"));
        QCOMPARE(o.value("odbc_t").toString(), QString("12:34:56.789"));
        QCOMPARE(o.value("odbc_ts").toString(), QString("2026-08-30T12:34:56.789"));
    }

    /// NaN/Infinity are not json numbers; QJsonDocument would write them as
    /// null, which hides what the server returned.
    void nonFiniteDoublesStayVisible()
    {
        const Row row{
            c("nan", QVariant(qQNaN())),
            c("inf", QVariant(qInf())),
        };
        const QJsonObject o = parsed(objectText(row)).object();
        QVERIFY(o.value("nan").isString());
        QVERIFY(o.value("inf").isString());
    }

    void quotingAndControlCharacters()
    {
        const Row row{
            c("q", QString("he said \"hi\"")),
            c("bs", QString("back\\slash")),
            c("nl", QString("line1\nline2\ttabbed")),
            c("ctl", QString("bell\x07here")),
            c("uni", QString::fromUtf8("\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87")), // cyrillic
        };
        const QJsonObject o = parsed(objectText(row)).object();

        QCOMPARE(o.value("q").toString(), QString("he said \"hi\""));
        QCOMPARE(o.value("bs").toString(), QString("back\\slash"));
        QCOMPARE(o.value("nl").toString(), QString("line1\nline2\ttabbed"));
        QCOMPARE(o.value("ctl").toString(), QString("bell\x07here"));
        QCOMPARE(o.value("uni").toString(), QString::fromUtf8("\xd0\xba\xd0\xbb\xd1\x8e\xd1\x87"));
    }

    void emptyRowAndEmptySelection()
    {
        QCOMPARE(objectText(Row{}), QString("{}"));
        QCOMPARE(arrayText(QVector<Row>{}), QString("[]"));
    }

    /// Several rows: an array of objects, one per row.
    void arrayOfRows()
    {
        const QVector<Row> rows{
            Row{c("id", 1), c("name", QString("first"))},
            Row{c("id", 2), c("name", QString("second"))},
        };
        const QString text = arrayText(rows);
        const QJsonDocument doc = parsed(text);
        QVERIFY(doc.isArray());
        const QJsonArray a = doc.array();
        QCOMPARE(a.size(), 2);
        QCOMPARE(a[0].toObject().value("name").toString(), QString("first"));
        QCOMPARE(a[1].toObject().value("id").toInt(), 2);
    }

    /// Nested documents inside an array of rows must not break the indentation
    /// (this is where a naive concatenation produces invalid json).
    void arrayWithNestedJsonStaysValid()
    {
        const QVector<Row> rows{
            Row{c("id", 1), c("doc", QString("{\"a\": {\"b\": [1, 2]}}"))},
            Row{c("id", 2), c("doc", QString("[{\"c\": 3}]"))},
        };
        const QJsonDocument doc = parsed(arrayText(rows));
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 2);
        QVERIFY(doc.array()[0].toObject().value("doc").isObject());
        QVERIFY(doc.array()[1].toObject().value("doc").isArray());
    }

    void duplicateColumnNamesAreKept()
    {
        // `select 1 a, 2 a` is legal sql; both columns must be visible even
        // though a json object with two identical keys is unusual (the parser
        // keeps the last one, but nothing is lost from the text).
        const Row row{c("a", 1), c("a", 2)};
        const QString text = objectText(row);
        QCOMPARE(text.count("\"a\""), 2);
        QVERIFY(parsed(text).isObject());
    }

    void isJsonDocumentRecognisesDocumentsOnly()
    {
        QVERIFY(isJsonDocument("{\"a\": 1}"));
        QVERIFY(isJsonDocument("  [1, 2]  "));
        QVERIFY(isJsonDocument("{\n  \"a\": 1\n}")); // formatted
        QVERIFY(!isJsonDocument("123"));            // a scalar is not a document
        QVERIFY(!isJsonDocument("\"text\""));
        QVERIFY(!isJsonDocument("{broken"));
        QVERIFY(!isJsonDocument(""));
        QVERIFY(!isJsonDocument("   "));
        QVERIFY(!isJsonDocument("hello"));
    }

    void kindFromTypeName()
    {
        QVERIFY(kindForTypeName("json") == Kind::Json);
        QVERIFY(kindForTypeName("jsonb") == Kind::Json);
        QVERIFY(kindForTypeName("JSONB") == Kind::Json);
        QVERIFY(kindForTypeName("numeric") == Kind::Numeric);
        QVERIFY(kindForTypeName("numeric(10,2)") == Kind::Numeric);
        QVERIFY(kindForTypeName("decimal(16,0)") == Kind::Numeric);
        QVERIFY(kindForTypeName("money") == Kind::Numeric);
        QVERIFY(kindForTypeName("text") == Kind::Auto);
        QVERIFY(kindForTypeName("timestamptz") == Kind::Auto);
        QVERIFY(kindForTypeName("") == Kind::Auto);
        // float8 is a double already - not a Numeric string
        QVERIFY(kindForTypeName("double precision") == Kind::Auto);
        // an array is the dbms' own syntax, never json - see below
        QVERIFY(kindForTypeName("integer[]") == Kind::Text);
        QVERIFY(kindForTypeName("jsonb[]") == Kind::Text);
        QVERIFY(kindForTypeName("numeric(10,2)[]") == Kind::Text);
    }

    /// postgres prints an array as `{1,2,3}` and an empty one as `{}`. `{}` is
    /// valid json, so guessing would show an empty array as an empty object
    /// while a filled one stayed text - the same column looking different from
    /// one row to the next.
    void arrayColumnsAreAlwaysStrings()
    {
        const Row row{
            c("arr", QString("{1,2,3}"), Kind::Text),
            c("empty", QString("{}"), Kind::Text),
            c("jarr", QString("{\"{\\\"x\\\": 1}\"}"), Kind::Text),
        };
        const QJsonObject o = parsed(objectText(row)).object();
        QVERIFY2(o.value("arr").isString(), "a pg array must stay a string");
        QVERIFY2(o.value("empty").isString(), "an empty pg array must not become {}");
        QCOMPARE(o.value("empty").toString(), QString("{}"));
        QVERIFY(o.value("jarr").isString());
    }

    /// A nested document is indented one level in from its key - a fixed indent,
    /// so a long column name does not push the value into a ragged far-right
    /// column (which is the sideways reading this feature exists to avoid).
    void nestedDocumentsUseFixedIndent()
    {
        const Row row{c("a_very_long_column_name_indeed",
                        QString("{\"a\": 1, \"b\": 2}"))};
        const QString text = objectText(row);
        QVERIFY(parsed(text).isObject());

        const QStringList lines = text.split('\n');
        // the nested object's inner lines sit at 8 spaces (one level in from the
        // key's 4), never at the key's own width
        bool sawInner = false;
        for (const QString &l: lines)
        {
            if (l.contains("\"a\":"))
            {
                sawInner = true;
                QCOMPARE(l.length() - l.trimmed().length(), 8);
            }
        }
        QVERIFY(sawInner);
    }

    /// A single cell on its own (the "one cell, not json" case shows the row,
    /// but valueText is what the row is built out of).
    void valueTextIndentsNestedDocuments()
    {
        const QString text = valueText(c("x", QString("{\"a\": 1, \"b\": 2}")), 4);
        // every continuation line carries the requested indent
        const QStringList lines = text.split('\n');
        QVERIFY(lines.size() > 1);
        for (int i = 1; i < lines.size(); ++i)
            QVERIFY2(lines[i].startsWith("    "), qPrintable(lines[i]));
    }

    // ---- the selection rules Ctrl+J follows ----

    /// One cell holding json: that json alone, and *not* flagged preformatted -
    /// the viewer's own expansion of nested escaped json still applies to it.
    void selectionSingleJsonCell()
    {
        TestModel model;
        bool preformatted = true;
        const QString text = forSelection(&model, {model.index(0, model.jsonColumn())},
                                          model.typeNameFn(), &preformatted);
        QCOMPARE(preformatted, false);
        QCOMPARE(text.trimmed(), model.jsonCellText());
    }

    /// One cell that is not json: the whole row, so a wide row can be read
    /// without scrolling sideways.
    void selectionSingleNonJsonCellGivesWholeRow()
    {
        TestModel model;
        bool preformatted = false;
        const QString text = forSelection(&model, {model.index(0, 0)},
                                          model.typeNameFn(), &preformatted);
        QCOMPARE(preformatted, true);

        const QJsonObject o = parsed(text).object();
        // every column of that row is present
        QCOMPARE(o.keys().size(), model.columnCount());
        QCOMPARE(o.value("id").toInt(), 1);
        QCOMPARE(o.value("name").toString(), QString("first"));
        // and the json column came out as a node, not an escaped string
        QVERIFY(o.value("doc").isObject());
        // column order is the query's, not alphabetical
        QVERIFY(text.indexOf("\"id\"") < text.indexOf("\"name\""));
    }

    /// Several cells of one row: only those cells.
    void selectionSeveralCellsOfOneRow()
    {
        TestModel model;
        bool preformatted = false;
        const QString text = forSelection(&model,
                                          {model.index(0, 2), model.index(0, 0)},
                                          model.typeNameFn(), &preformatted);
        QCOMPARE(preformatted, true);

        const QJsonObject o = parsed(text).object();
        QCOMPARE(o.keys().size(), 2);
        QVERIFY(o.contains("id"));
        QVERIFY(o.contains("amount"));
        QVERIFY(!o.contains("name"));
        // given out of order, rendered in column order
        QVERIFY(text.indexOf("\"id\"") < text.indexOf("\"amount\""));
    }

    /// Several cells across rows: an array of one object per row.
    void selectionSeveralRowsGivesArray()
    {
        TestModel model;
        bool preformatted = false;
        const QString text = forSelection(&model,
                                          {model.index(1, 1), model.index(0, 0),
                                           model.index(1, 0), model.index(0, 1)},
                                          model.typeNameFn(), &preformatted);
        QCOMPARE(preformatted, true);

        const QJsonDocument doc = parsed(text);
        QVERIFY(doc.isArray());
        const QJsonArray a = doc.array();
        QCOMPARE(a.size(), 2);
        // rows in display order, each with only the selected columns
        QCOMPARE(a[0].toObject().value("id").toInt(), 1);
        QCOMPARE(a[0].toObject().keys().size(), 2);
        QCOMPARE(a[1].toObject().value("id").toInt(), 2);
        QCOMPARE(a[1].toObject().value("name").toString(), QString("second"));
    }

    /// A json cell selected together with others is expanded in place, as a
    /// nested node - the readable form the user asked for.
    void selectionKeepsJsonCellsAsNodes()
    {
        TestModel model;
        const QString text = forSelection(&model,
                                          {model.index(0, 0), model.index(0, model.jsonColumn())},
                                          model.typeNameFn(), nullptr);
        const QJsonObject o = parsed(text).object();
        QVERIFY(o.value("doc").isObject());
        QCOMPARE(o.value("doc").toObject().value("nested").toObject().value("deep").toInt(), 7);
    }

    /// The row of a cell that merely looks like json must still be valid json.
    void selectionRowWithBraceLeadingTextStaysValid()
    {
        TestModel model;
        // row 1 holds "{not really json" in its doc column
        const QString text = forSelection(&model, {model.index(1, 0)},
                                          model.typeNameFn(), nullptr);
        const QJsonObject o = parsed(text).object();
        QVERIFY(o.value("doc").isString());
        QCOMPARE(o.value("doc").toString(), QString("{not really json"));
    }

    /// numeric columns keep their digits even when reached through the model.
    void selectionNumericColumnIsString()
    {
        TestModel model;
        const QString text = forSelection(&model, {model.index(0, 0)},
                                          model.typeNameFn(), nullptr);
        const QJsonObject o = parsed(text).object();
        QVERIFY(o.value("amount").isString());
        QCOMPARE(o.value("amount").toString(), QString("12345678901234567890.12345"));
    }

    void selectionWithoutTypeNamesStillWorks()
    {
        TestModel model;
        // no type callback at all: json in a text column is still expanded,
        // only the explicit json/numeric handling is missing
        const QString text = forSelection(&model, {model.index(0, 0)}, nullptr, nullptr);
        const QJsonObject o = parsed(text).object();
        QVERIFY(o.value("doc").isObject());
        // without the numeric hint it is a plain string value here
        QVERIFY(!o.value("amount").isNull());
    }

    void selectionEmptyAndInvalid()
    {
        TestModel model;
        QVERIFY(forSelection(&model, {}, model.typeNameFn(), nullptr).isNull());
        QVERIFY(forSelection(nullptr, {model.index(0, 0)}, model.typeNameFn(), nullptr).isNull());
        QVERIFY(forSelection(&model, {QModelIndex()}, model.typeNameFn(), nullptr).isNull());
    }
};

QTEST_APPLESS_MAIN(TestRowJson)
#include "tst_rowjson.moc"
