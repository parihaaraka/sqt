#include <QtTest>
#include "pgtypmod.h"
#include "pgtypes.h"

/// The expected strings below are what the server's own format_type() prints for
/// the very same (oid, typmod) pair - taken from a live postgres.
class TestPgTypmod : public QObject
{
    Q_OBJECT

private:
    /// oid/typmod pairs are unreadable on their own, so failures name the case
    static void check(const char *what, int oid, int typmod,
                      const QString &suffix, int length, int scale)
    {
        const PgTypmod tm = pgDecodeTypmod(oid, typmod);
        QVERIFY2(tm.suffix == suffix,
                 qPrintable(QString("%1: got \"%2\", expected \"%3\"")
                            .arg(what, tm.suffix, suffix)));
        QVERIFY2(tm.length == length,
                 qPrintable(QString("%1: length %2, expected %3")
                            .arg(what).arg(tm.length).arg(length)));
        QVERIFY2(tm.scale == scale,
                 qPrintable(QString("%1: scale %2, expected %3")
                            .arg(what).arg(tm.scale).arg(scale)));
    }

private slots:
    /// -1 is "no modifier given" for every type
    void noModifier()
    {
        check("numeric", NUMERICOID, -1, QString(), -1, -1);
        check("text", TEXTOID, -1, QString(), -1, -1);
        check("bpchar", BPCHAROID, -1, QString(), -1, -1);
        check("timestamp", TIMESTAMPOID, -1, QString(), -1, -1);
        check("interval", INTERVALOID, -1, QString(), -1, -1);
        check("money", CASHOID, -1, QString(), -1, -1);
    }

    /// the case from the bug report: precision and scale live in the two halves
    /// of (typmod - VARHDRSZ), and both are always printed - decimal(16,0) is
    /// not numeric(16)
    void numericKeepsPrecisionAndScale()
    {
        check("numeric(16,0)", NUMERICOID, 1048580, "(16,0)", 16, 0);
        check("numeric(10,2)", NUMERICOID, 655366, "(10,2)", 10, 2);
        check("numeric(5,0)", NUMERICOID, 327684, "(5,0)", 5, 0);
        check("numeric(1000,999)", NUMERICOID, 65537003, "(1000,999)", 1000, 999);
    }

    /// pg15 and later allow a negative scale (numeric(10,-2) rounds to hundreds)
    /// and keep it sign-extended in 11 bits
    void numericScaleMayBeNegative()
    {
        check("numeric(10,-2)", NUMERICOID, 657410, "(10,-2)", 10, -2);
    }

    void bitLengthIsStoredAsIs()
    {
        check("bit(5)", BITOID, 5, "(5)", 5, -1);
        check("bit(1)", BITOID, 1, "(1)", 1, -1);
        check("varbit(7)", VARBITOID, 7, "(7)", 7, -1);
    }

    /// no VARHDRSZ offset here, which is why the precision used to be dropped
    /// as "less than a varlena header"
    void datetimePrecisionSurvives()
    {
        check("timestamp(3)", TIMESTAMPOID, 3, "(3)", 3, -1);
        check("timestamp(0)", TIMESTAMPOID, 0, "(0)", 0, -1);
        check("timestamptz(6)", TIMESTAMPTZOID, 6, "(6)", 6, -1);
        check("time(0)", TIMEOID, 0, "(0)", 0, -1);
        check("timetz(4)", TIMETZOID, 4, "(4)", 4, -1);
    }

    /// an interval spells out the fields it spans, and adds the seconds
    /// precision only when one was given
    void intervalFieldsAndPrecision()
    {
        check("interval year", INTERVALOID, 327679, " year", -1, -1);
        check("interval month", INTERVALOID, 196607, " month", -1, -1);
        check("interval year to month", INTERVALOID, 458751, " year to month", -1, -1);
        check("interval day", INTERVALOID, 589823, " day", -1, -1);
        check("interval hour", INTERVALOID, 67174399, " hour", -1, -1);
        check("interval minute", INTERVALOID, 134283263, " minute", -1, -1);
        check("interval second", INTERVALOID, 268500991, " second", -1, -1);
        check("interval day to hour", INTERVALOID, 67698687, " day to hour", -1, -1);
        check("interval day to minute", INTERVALOID, 201916415, " day to minute", -1, -1);
        check("interval day to second(3)", INTERVALOID, 470286339, " day to second(3)", 3, -1);
        check("interval hour to minute", INTERVALOID, 201392127, " hour to minute", -1, -1);
        check("interval hour to second", INTERVALOID, 469827583, " hour to second", -1, -1);
        check("interval minute to second", INTERVALOID, 402718719, " minute to second", -1, -1);
        check("interval second(2)", INTERVALOID, 268435458, " second(2)", 2, -1);
        // the full range with a precision of its own
        check("interval(2)", INTERVALOID, 2147418114, "(2)", 2, -1);
        check("interval(0)", INTERVALOID, 2147418112, "(0)", 0, -1);
    }

    /// varchar/bpchar keep length + VARHDRSZ, and so does the fallback for any
    /// type sqt does not know about
    void varlenaLengthLosesTheHeader()
    {
        check("varchar(10)", VARCHAROID, 14, "(10)", 10, -1);
        check("bpchar(3)", BPCHAROID, 7, "(3)", 3, -1);
        check("varchar(5)", VARCHAROID, 9, "(5)", 5, -1);
        // a bare VARHDRSZ would mean a length of zero, which no declaration can
        // produce - the server prints no modifier for it either
        check("varchar(0)", VARCHAROID, 4, QString(), -1, -1);
        check("bpchar(0)", BPCHAROID, 4, QString(), -1, -1);
    }

};

QTEST_APPLESS_MAIN(TestPgTypmod)
#include "tst_pgtypmod.moc"
