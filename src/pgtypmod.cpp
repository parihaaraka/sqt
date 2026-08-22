#include "pgtypmod.h"
#include "pgtypes.h"

namespace {

/// Interval keeps the set of fields it spans in the high half of its typmod.
/// The bit numbers are postgres' own field codes (see utils/datetime.h), and
/// the low half holds the seconds precision.
enum IntervalField { IvMonth = 1, IvYear = 2, IvDay = 3, IvHour = 10, IvMinute = 11, IvSecond = 12 };
constexpr int ivMask(IntervalField f) { return 1 << int(f); }
constexpr int IntervalFullRange = 0x7fff;
constexpr int IntervalFullPrecision = 0xffff;

/// The " day to second" part of an interval's declaration, as
/// intervaltypmodout() spells it - leading space included, empty for an
/// interval spanning everything.
QString intervalFields(int range)
{
    switch (range)
    {
    case ivMask(IvYear):                                                       return QStringLiteral(" year");
    case ivMask(IvMonth):                                                      return QStringLiteral(" month");
    case ivMask(IvYear) | ivMask(IvMonth):                                     return QStringLiteral(" year to month");
    case ivMask(IvDay):                                                        return QStringLiteral(" day");
    case ivMask(IvHour):                                                       return QStringLiteral(" hour");
    case ivMask(IvMinute):                                                     return QStringLiteral(" minute");
    case ivMask(IvSecond):                                                     return QStringLiteral(" second");
    case ivMask(IvDay) | ivMask(IvHour):                                       return QStringLiteral(" day to hour");
    case ivMask(IvDay) | ivMask(IvHour) | ivMask(IvMinute):                    return QStringLiteral(" day to minute");
    case ivMask(IvDay) | ivMask(IvHour) | ivMask(IvMinute) | ivMask(IvSecond): return QStringLiteral(" day to second");
    case ivMask(IvHour) | ivMask(IvMinute):                                    return QStringLiteral(" hour to minute");
    case ivMask(IvHour) | ivMask(IvMinute) | ivMask(IvSecond):                 return QStringLiteral(" hour to second");
    case ivMask(IvMinute) | ivMask(IvSecond):                                  return QStringLiteral(" minute to second");
    }
    // IntervalFullRange, and anything a future server may invent
    return QString();
}

} // anonymous namespace

PgTypmod pgDecodeTypmod(int typeOid, int typmod)
{
    PgTypmod res;
    if (typmod < 0)
        return res;

    switch (typeOid)
    {
    case NUMERICOID:
    {
        if (typmod < VARHDRSZ)   // no precision given, nothing to decode
            return res;
        const int packed = typmod - VARHDRSZ;
        res.length = (packed >> 16) & 0xffff;
        // Scale may be negative since pg15 (numeric(10,-2) rounds to hundreds),
        // and is kept sign-extended in 11 bits. Older servers store a plain
        // 0..1000 there, which this decodes unchanged.
        res.scale = int16_t(((packed & 0x7ff) ^ 1024) - 1024);
        // format_type() always prints both parts, so numeric(16,0) does not
        // silently become numeric(16)
        res.suffix = '(' + QString::number(res.length) + ',' + QString::number(res.scale) + ')';
        return res;
    }

    case BITOID:
    case VARBITOID:
        // the length is stored as is
        res.length = typmod;
        res.suffix = '(' + QString::number(res.length) + ')';
        return res;

    case TIMEOID:
    case TIMETZOID:
    case TIMESTAMPOID:
    case TIMESTAMPTZOID:
        // A bare seconds precision, 0 included - no VARHDRSZ offset here, which
        // is why timestamp(3) used to lose its precision (3 looked like "less
        // than a varlena header, so not a real modifier").
        res.length = typmod;
        res.suffix = '(' + QString::number(res.length) + ')';
        return res;

    case INTERVALOID:
    {
        const int precision = typmod & 0xffff;
        res.suffix = intervalFields((typmod >> 16) & IntervalFullRange);
        if (precision != IntervalFullPrecision)
        {
            res.length = precision;
            res.suffix += '(' + QString::number(precision) + ')';
        }
        return res;
    }
    }

    // varchar/bpchar keep length + VARHDRSZ, and so does every other varlena
    // type of the standard set. An extension type with a typmod of its own
    // (postgis' geometry, say) needs its typmodout function to be read
    // correctly - there is no way to do that here, so it keeps this guess.
    //
    // Strictly greater, as varchartypmodout() has it: a length of zero is not a
    // legal declaration, and the server prints no modifier at all for it.
    if (typmod > VARHDRSZ)
    {
        res.length = typmod - VARHDRSZ;
        res.suffix = '(' + QString::number(res.length) + ')';
    }
    return res;
}
