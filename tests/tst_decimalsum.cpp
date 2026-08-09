#include <QtTest>
#include <QLocale>
#include "decimalsum.h"

/// Sums text as a dbms prints it, so the cases below are values seen in real
/// result sets: wide numerics, bigints past int64, floats in exponential form.
class TestDecimalSum : public QObject
{
    Q_OBJECT

private:
    /// sums the values in order and returns the plain text of the total
    static QString sum(const QStringList &values, int *accepted = nullptr)
    {
        DecimalSum s;
        for (const QString &v: values)
            s.add(v);
        if (accepted)
            *accepted = s.count();
        return s.toString();
    }

private slots:
    void empty()
    {
        DecimalSum s;
        QVERIFY(s.isEmpty());
        QCOMPARE(s.count(), 0);
        QCOMPARE(s.toString(), QString("0"));
    }

    void integers()
    {
        QCOMPARE(sum({"1", "2", "3"}), QString("6"));
        QCOMPARE(sum({"0"}), QString("0"));
        QCOMPARE(sum({"-1", "-2"}), QString("-3"));
        QCOMPARE(sum({"007", "3"}), QString("10"));
        QCOMPARE(sum({"+5", "5"}), QString("10"));
    }

    /// the very reason the module exists: double loses these, int64 overflows
    void beyondNativeTypes()
    {
        // 2^53 + 1 is the first integer a double cannot tell from its neighbour
        QCOMPARE(sum({"9007199254740993"}), QString("9007199254740993"));
        QCOMPARE(sum({"9007199254740992", "1"}), QString("9007199254740993"));
        // int64 tops out here
        QCOMPARE(sum({"9223372036854775807", "1"}), QString("9223372036854775808"));
        QCOMPARE(sum({"9223372036854775807", "9223372036854775807"}),
                 QString("18446744073709551614"));
        // a numeric far wider than any native type
        QCOMPARE(sum({"123456789012345678901234567890",
                      "876543210987654321098765432110"}),
                 QString("1000000000000000000000000000000"));
        // the classic binary floating point failure
        QCOMPARE(sum({"0.1", "0.2"}), QString("0.3"));
        QCOMPARE(sum({"1.1", "2.2"}), QString("3.3"));
    }

    void fractions()
    {
        QCOMPARE(sum({"1.25", "2.5"}), QString("3.75"));
        // the widest fraction sets the scale of the total
        QCOMPARE(sum({"1.5", "1"}), QString("2.5"));
        QCOMPARE(sum({"1", "1.500"}), QString("2.500"));
        QCOMPARE(sum({".5", ".5"}), QString("1.0"));
        QCOMPARE(sum({"0.001", "0.002"}), QString("0.003"));
        QCOMPARE(sum({"-0.75", "0.25"}), QString("-0.50"));
    }

    void carriesAndBorrows()
    {
        QCOMPARE(sum({"999999999999999999999", "1"}), QString("1000000000000000000000"));
        QCOMPARE(sum({"1000000000000000000000", "-1"}), QString("999999999999999999999"));
        QCOMPARE(sum({"0.999999999999999999999", "0.000000000000000000001"}),
                 QString("1.000000000000000000000"));
        QCOMPARE(sum({"1", "-0.000000000000000000001"}),
                 QString("0.999999999999999999999"));
    }

    void signChanges()
    {
        QCOMPARE(sum({"1", "-3"}), QString("-2"));
        QCOMPARE(sum({"-1", "3"}), QString("2"));
        // a zero total never carries a sign
        QCOMPARE(sum({"5", "-5"}), QString("0"));
        QCOMPARE(sum({"-0"}), QString("0"));
        QCOMPARE(sum({"-0.0"}), QString("0.0"));
        QCOMPARE(sum({"1.5", "-1.5"}), QString("0.0"));
        // wandering across zero must not disturb the total
        QCOMPARE(sum({"1", "-3", "5", "-10", "7"}), QString("0"));
    }

    /// float4/float8 arrive in exponential form
    void exponentialForm()
    {
        QCOMPARE(sum({"1e+30"}), QString("1000000000000000000000000000000"));
        QCOMPARE(sum({"1E-7"}), QString("0.0000001"));
        QCOMPARE(sum({"1.5e2"}), QString("150"));
        QCOMPARE(sum({"1.5e-2"}), QString("0.015"));
        QCOMPARE(sum({"-2.5e1", "5"}), QString("-20"));
        // the exponent outgrows the fraction, so the value is a whole one
        QCOMPARE(sum({"1.5e2", "1"}), QString("151"));
        QCOMPARE(sum({"1e+30", "-1e+30"}), QString("0"));
    }

    /// anything but a number stays out of the sum instead of poisoning it
    void rejects()
    {
        DecimalSum s;
        const QStringList garbage = {
            "", " ", "abc", "NaN", "nan", "Infinity", "-Infinity", "inf",
            "1.2.3", "1e", "1e+", "e5", "12abc", "1 000", "1,5", "$5", "--1",
            "0x10", "+", "-", "."
        };
        for (const QString &g: garbage)
            QVERIFY2(!s.add(g), qPrintable("accepted: '" + g + "'"));
        QCOMPARE(s.count(), 0);
        QCOMPARE(s.toString(), QString("0"));

        // a rejected value leaves the running total untouched
        int accepted = 0;
        QCOMPARE(sum({"2", "NaN", "3", "text"}, &accepted), QString("5"));
        QCOMPARE(accepted, 2);
    }

    void whitespaceIsTolerated()
    {
        QCOMPARE(sum({" 1 ", "\t2\n"}), QString("3"));
    }

    void scaleReported()
    {
        DecimalSum s;
        s.add("1");
        QCOMPARE(s.scale(), 0);
        s.add("0.001");
        QCOMPARE(s.scale(), 3);
        s.add("0.5");
        QCOMPARE(s.scale(), 3);
        QCOMPARE(s.count(), 3);
        QCOMPARE(s.toString(), QString("1.501"));
    }

    /// a long run of the same value is where rounding used to creep in
    void manyValues()
    {
        DecimalSum s;
        for (int i = 0; i < 1000; ++i)
            s.add("0.01");
        QCOMPARE(s.toString(), QString("10.00"));
        QCOMPARE(s.count(), 1000);

        DecimalSum big;
        for (int i = 0; i < 100; ++i)
            big.add("99999999999999999999.99");
        QCOMPARE(big.toString(), QString("9999999999999999999999.00"));
    }

    void localeFormatting()
    {
        DecimalSum s;
        s.add("1234567.89");
        // the C locale omits group separators by definition
        QCOMPARE(s.toString(QLocale::c()), QString("1234567.89"));
        QCOMPARE(s.toString(QLocale(QLocale::English, QLocale::UnitedStates)),
                 QString("1,234,567.89"));

        DecimalSum n;
        n.add("-1000");
        QCOMPARE(n.toString(QLocale(QLocale::English, QLocale::UnitedStates)),
                 QString("-1,000"));

        DecimalSum wide;
        wide.add("123456789012345678901234.5");
        QCOMPARE(wide.toString(QLocale(QLocale::English, QLocale::UnitedStates)),
                 QString("123,456,789,012,345,678,901,234.5"));
    }
};

QTEST_APPLESS_MAIN(TestDecimalSum)

#include "tst_decimalsum.moc"
