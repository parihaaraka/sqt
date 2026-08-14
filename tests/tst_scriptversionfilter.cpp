#include <QtTest>
#include "scriptversionfilter.h"

using Scripting::versionSpecificPart;

class TestScriptVersionFilter : public QObject
{
    Q_OBJECT

private slots:
    void extractsVersionSpecificPart_data();
    void extractsVersionSpecificPart();
    void rejectsUnbalancedBoundaries_data();
    void rejectsUnbalancedBoundaries();
};

void TestScriptVersionFilter::extractsVersionSpecificPart_data()
{
    QTest::addColumn<QString>("script");
    QTest::addColumn<int>("version");
    QTest::addColumn<QString>("expected");

    QTest::newRow("no boundaries")
            << "select 1" << 100000 << "select 1";

    const QString single = "A/* if version 90000 */B/* endif version */C";
    QTest::newRow("if taken")     << single << 100000 << "ABC";
    QTest::newRow("if skipped")   << single << 80000  << "AC";
    QTest::newRow("if exact")     << single << 90000  << "ABC";

    const QString withElse =
            "/* if version 100000 */IF/* else version */ELSE/* endif version */T";
    QTest::newRow("else skipped") << withElse << 100000 << "IFT";
    QTest::newRow("else taken")   << withElse << 90000  << "ELSET";

    const QString chain =
            "/* if version 100000 */IF"
            "/* elif version 90000 */ELIF"
            "/* else version */ELSE"
            "/* endif version */T";
    QTest::newRow("chain: if")   << chain << 100000 << "IFT";
    QTest::newRow("chain: elif") << chain << 95000  << "ELIFT";
    QTest::newRow("chain: else") << chain << 80000  << "ELSET";

    // the first suitable branch wins even though the later ones match as well
    const QString descending =
            "/* if version 100000 */NEW"
            "/* elif version 90000 */MID"
            "/* elif version 80000 */OLD"
            "/* endif version */T";
    QTest::newRow("first match wins")   << descending << 110000 << "NEWT";
    QTest::newRow("second match wins")  << descending << 95000  << "MIDT";
    QTest::newRow("last branch")        << descending << 85000  << "OLDT";
    QTest::newRow("no branch")          << descending << 70000  << "T";

    // a block following a skipped one must still be processed
    const QString sequential =
            "/* if version 100000 */V1/* endif version */"
            "M"
            "/* if version 90000 */V2/* endif version */T";
    QTest::newRow("both blocks")   << sequential << 100000 << "V1MV2T";
    QTest::newRow("second only")   << sequential << 95000  << "MV2T";
    QTest::newRow("neither block") << sequential << 80000  << "MT";

    const QString nested =
            "/* if version 100000 */O1"
            "/* if version 80000 */I/* endif version */"
            "O2/* endif version */T";
    QTest::newRow("nested: outer taken")   << nested << 100000 << "O1IO2T";
    QTest::newRow("nested: outer skipped") << nested << 90000  << "T";

    // a nested block inside a taken else branch
    const QString nestedInElse =
            "/* if version 100000 */IF/* else version */E1"
            "/* if version 90000 */NI/* endif version */"
            "E2/* endif version */T";
    QTest::newRow("nested in else: inner taken")   << nestedInElse << 95000 << "E1NIE2T";
    QTest::newRow("nested in else: inner skipped") << nestedInElse << 80000 << "E1E2T";
    QTest::newRow("nested in else: else skipped")  << nestedInElse << 100000 << "IFT";

    // an inner block must stay skipped along with its skipped parent, however
    // well its own version matches
    QTest::newRow("nested: inner matches, outer does not")
            << "/* if version 100000 */O/* if version 1 */I/* endif version *//* endif version */T"
            << 90000 << "T";
}

void TestScriptVersionFilter::extractsVersionSpecificPart()
{
    QFETCH(QString, script);
    QFETCH(int, version);
    QFETCH(QString, expected);

    QCOMPARE(versionSpecificPart(script, version), expected);
}

void TestScriptVersionFilter::rejectsUnbalancedBoundaries_data()
{
    QTest::addColumn<QString>("script");

    QTest::newRow("endif without if")  << "A/* endif version */B";
    QTest::newRow("elif without if")   << "A/* elif version 90000 */B";
    QTest::newRow("else without if")   << "A/* else version */B";
    QTest::newRow("unclosed if")       << "/* if version 90000 */A";
    QTest::newRow("unclosed nested if")
            << "/* if version 90000 */A/* if version 80000 */B/* endif version */";
    QTest::newRow("extra endif")
            << "/* if version 90000 */A/* endif version *//* endif version */";
}

void TestScriptVersionFilter::rejectsUnbalancedBoundaries()
{
    QFETCH(QString, script);

#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    QVERIFY_EXCEPTION_THROWN(versionSpecificPart(script, 100000), QString);
#else
    QVERIFY_THROWS_EXCEPTION(QString, versionSpecificPart(script, 100000));
#endif
}

QTEST_APPLESS_MAIN(TestScriptVersionFilter)

#include "tst_scriptversionfilter.moc"
