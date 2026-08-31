#include <QtTest>
#include <clocale>
#include <cmath>
#include "misc.h"

/// parseDouble() replaces atof()/strtod() at the places where machine-readable
/// text is turned into a number: values off a dbms connection, sizes inside a
/// stylesheet. The point of it is that the result must not depend on the
/// locale - which is exactly what the old code got wrong, since QApplication
/// applies the system locale on startup and half of Europe writes 1,5.
class TestParseDouble : public QObject
{
    Q_OBJECT

private:
    /// Every check runs under each of these, so a locale-dependent parse cannot
    /// pass. "C" is the baseline; the comma locales are the ones that broke it.
    /// A locale the box does not have is skipped by setlocale() returning null,
    /// which is reported rather than silently passing.
    static QList<QByteArray> numericLocales()
    {
        return {"C", "ru_RU.UTF-8", "de_DE.UTF-8", "fr_FR.UTF-8"};
    }

    /// Runs \a fn under every available comma/dot locale, restoring the previous
    /// one afterwards. Returns how many locales were actually exercised.
    int forEachLocale(std::function<void(const char*)> fn)
    {
        const QByteArray saved(setlocale(LC_NUMERIC, nullptr));
        int used = 0;
        const QList<QByteArray> locales = numericLocales();
        for (const QByteArray &loc: locales)
        {
            if (!setlocale(LC_NUMERIC, loc.constData()))
            {
                qInfo("locale %s not available here - skipped", loc.constData());
                continue;
            }
            ++used;
            fn(loc.constData());
        }
        setlocale(LC_NUMERIC, saved.constData());
        return used;
    }

private slots:
    /// The bug itself: "1.5" must be 1.5 everywhere, never 1.
    void fractionSurvivesEveryLocale()
    {
        const int used = forEachLocale([](const char *loc) {
            bool ok = false;
            const double d = parseDouble("1.5", &ok);
            QVERIFY2(ok, loc);
            QVERIFY2(qFuzzyCompare(d, 1.5),
                     qPrintable(QString("%1: got %2, expected 1.5").arg(loc).arg(d, 0, 'g', 17)));
        });
        QVERIFY2(used >= 2, "only one locale was available - the test proves little");
    }

    void signsAndExponents()
    {
        forEachLocale([](const char *loc) {
            bool ok = false;
            QVERIFY2(qFuzzyCompare(parseDouble("-1.5", &ok), -1.5), loc);
            QVERIFY2(ok, loc);
            // strtod accepts a leading '+', so the replacement does too
            QVERIFY2(qFuzzyCompare(parseDouble("+1.5", &ok), 1.5), loc);
            QVERIFY2(ok, loc);
            QVERIFY2(qFuzzyCompare(parseDouble("1e-300", &ok), 1e-300), loc);
            QVERIFY2(qFuzzyCompare(parseDouble("3.4e+38", &ok), 3.4e+38), loc);
            QVERIFY2(qFuzzyCompare(parseDouble("  2.25", &ok), 2.25), loc); // leading space
            QVERIFY2(qFuzzyCompare(parseDouble("0.1", &ok), 0.1), loc);
        });
    }

    /// What postgres prints for a float besides plain digits. atof() understood
    /// these, so the replacement has to as well - turning an infinity into 0
    /// would be a new bug in place of the old one.
    void postgresSpecialValues()
    {
        forEachLocale([](const char *loc) {
            bool ok = false;
            QVERIFY2(std::isnan(parseDouble("NaN", &ok)), loc);
            QVERIFY2(ok, loc);
            const double inf = parseDouble("Infinity", &ok);
            QVERIFY2(std::isinf(inf) && inf > 0, loc);
            QVERIFY2(ok, loc);
            const double ninf = parseDouble("-Infinity", &ok);
            QVERIFY2(std::isinf(ninf) && ninf < 0, loc);
            QVERIFY2(ok, loc);
            // the short spellings, in case a driver uses them
            QVERIFY2(std::isnan(parseDouble("nan", &ok)), loc);
            QVERIFY2(std::isinf(parseDouble("inf", &ok)), loc);
        });
    }

    /// Lenient where strtod was: parsing stops at the first character that
    /// cannot belong to the number. This is what the stylesheet reader needs -
    /// "9.5pt" is a size followed by a unit.
    void stopsAtTrailingText()
    {
        forEachLocale([](const char *loc) {
            bool ok = false;
            QVERIFY2(qFuzzyCompare(parseDouble("9.5pt", &ok), 9.5), loc);
            QVERIFY2(ok, loc);
            QVERIFY2(qFuzzyCompare(parseDouble("12px", &ok), 12.0), loc);
            QVERIFY2(ok, loc);
        });
    }

    /// A comma is never a decimal separator here, whatever the locale says:
    /// these values come off a wire or out of a stylesheet, not from a keyboard.
    void commaIsNotASeparator()
    {
        forEachLocale([](const char *loc) {
            bool ok = false;
            // "1,5" is the number 1 followed by junk - and must not become 1.5
            const double d = parseDouble("1,5", &ok);
            QVERIFY2(ok, loc);
            QVERIFY2(qFuzzyCompare(d, 1.0),
                     qPrintable(QString("%1: got %2, expected 1").arg(loc).arg(d, 0, 'g', 17)));
        });
    }

    void rejectsWhatIsNotANumber()
    {
        forEachLocale([](const char *loc) {
            bool ok = true;
            QCOMPARE(parseDouble("", &ok), 0.0);
            QVERIFY2(!ok, loc);
            ok = true;
            QCOMPARE(parseDouble("abc", &ok), 0.0);
            QVERIFY2(!ok, loc);
            ok = true;
            QCOMPARE(parseDouble(nullptr, &ok), 0.0);
            QVERIFY2(!ok, loc);
            ok = true;
            QCOMPARE(parseDouble("   ", &ok), 0.0);
            QVERIFY2(!ok, loc);
            ok = true;
            QCOMPARE(parseDouble(".", &ok), 0.0);
            QVERIFY2(!ok, loc);
        });
    }

    void zeroIsAValidNumber()
    {
        forEachLocale([](const char *loc) {
            bool ok = false;
            QCOMPARE(parseDouble("0", &ok), 0.0);
            QVERIFY2(ok, loc);
            QCOMPARE(parseDouble("0.0", &ok), 0.0);
            QVERIFY2(ok, loc);
        });
    }

    /// The ok flag is optional - the dbms path calls it without one.
    void worksWithoutTheOkFlag()
    {
        forEachLocale([](const char *loc) {
            QVERIFY2(qFuzzyCompare(parseDouble("1.5"), 1.5), loc);
            QCOMPARE(parseDouble("nonsense"), 0.0);
        });
    }

    /// Full precision, not just the first few digits.
    void keepsFullPrecision()
    {
        forEachLocale([](const char *loc) {
            const double d = parseDouble("0.1234567890123456");
            QVERIFY2(qFuzzyCompare(d, 0.1234567890123456), loc);
            QVERIFY2(qFuzzyCompare(parseDouble("1e+308"), 1e+308), loc);
        });
    }
};

QTEST_APPLESS_MAIN(TestParseDouble)
#include "tst_parsedouble.moc"
