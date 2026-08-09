#include <QtTest>
#include <QJsonArray>
#include <QJsonObject>
#include "queryoptions.h"

/// The options live in /*sqt ... */ comments of the user's script, so anything
/// at all may arrive here. The cases below are about what a script is allowed to
/// say and what several of those comments add up to.
class TestQueryOptions : public QObject
{
    Q_OBJECT

private slots:
    void noCommentGivesNoOptions()
    {
        QVERIFY(QueryOptions::Extract("select 1").isEmpty());
        QVERIFY(QueryOptions::Extract(QString()).isEmpty());
        // a comment that is not ours
        QVERIFY(QueryOptions::Extract("/* just a comment */ select 1").isEmpty());
    }

    /// the marker is a word of its own
    void similarlyNamedMarkerIsNotOurs()
    {
        QVERIFY(QueryOptions::Extract("/*sqtx { \"interval\": 100 } */ select 1").isEmpty());
    }

    void intervalIsRead()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"interval\": 1000 } */\nselect 1");
        QCOMPARE(o["interval"].toInt(), 1000);
    }

    /// the sql after the closing */ is not json and must not spoil the parsing
    void queryAfterTheCommentIsIgnored()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"interval\": 5 } */\nselect * from t where x = '}'");
        QCOMPARE(o["interval"].toInt(), 5);
    }

    /// a single destination is spelled without an array, and the rest of the
    /// application still gets an array
    void singleValueIsWrappedIntoAnArray()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"copy_dst\": \"/tmp/a.csv\" } */ copy t to stdout");
        QVERIFY(o["copy_dst"].isArray());
        const QJsonArray dst = o["copy_dst"].toArray();
        QCOMPARE(dst.count(), 1);
        QCOMPARE(dst[0].toString(), QString("/tmp/a.csv"));
    }

    void arrayValueIsKept()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"copy_dst\": [\"a\", \"b\"] } */ copy t to stdout");
        QCOMPARE(o["copy_dst"].toArray().count(), 2);
    }

    /// every comment adds its own destinations, in the order they are written:
    /// the n-th file belongs to the n-th copy command
    void listsOfSeveralCommentsAreConcatenated()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"copy_dst\": \"a\" } */ copy t1 to stdout;\n"
                    "/*sqt { \"copy_dst\": [\"b\", \"c\"] } */ copy t2 to stdout;");
        const QJsonArray dst = o["copy_dst"].toArray();
        QCOMPARE(dst.count(), 3);
        QCOMPARE(dst[0].toString(), QString("a"));
        QCOMPARE(dst[1].toString(), QString("b"));
        QCOMPARE(dst[2].toString(), QString("c"));
    }

    /// unlike the lists, a scalar cannot be concatenated, so the first one wins
    void firstIntervalWins()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"interval\": 100 } */ select 1;\n"
                    "/*sqt { \"interval\": 900 } */ select 2;");
        QCOMPARE(o["interval"].toInt(), 100);
    }

    void chartsAreCollected()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"charts\": [{ \"name\": \"a\" }] } */ select 1;\n"
                    "/*sqt { \"charts\": [{ \"name\": \"b\" }] } */ select 2;");
        const QJsonArray charts = o["charts"].toArray();
        QCOMPARE(charts.count(), 2);
        QCOMPARE(charts[0].toObject()["name"].toString(), QString("a"));
        QCOMPARE(charts[1].toObject()["name"].toString(), QString("b"));
    }

    /// keys we know nothing about are none of our business
    void unknownKeysAreDropped()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"whatever\": 1, \"interval\": 7 } */ select 1");
        QVERIFY(!o.contains("whatever"));
        QCOMPARE(o["interval"].toInt(), 7);
    }

    /// a typo in the options must not cost the user the query
    void malformedJsonIsSurvived()
    {
        QVERIFY(QueryOptions::Extract("/*sqt { \"interval\": } */ select 1").isEmpty());
        QVERIFY(QueryOptions::Extract("/*sqt not a json at all */ select 1").isEmpty());
        QVERIFY(QueryOptions::Extract("/*sqt { \"interval\": 1 ").isEmpty());
        QVERIFY(QueryOptions::Extract("/*sqt").isEmpty());
    }

    /// a bad comment stops the parsing, but what has been read stays read
    void goodCommentBeforeABadOneIsKept()
    {
        const QJsonObject o = QueryOptions::Extract(
                    "/*sqt { \"interval\": 3 } */ select 1;\n"
                    "/*sqt oops */ select 2;");
        QCOMPARE(o["interval"].toInt(), 3);
    }
};

QTEST_APPLESS_MAIN(TestQueryOptions)

#include "tst_queryoptions.moc"
