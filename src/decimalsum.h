#ifndef DECIMALSUM_H
#define DECIMALSUM_H

#include <QString>
#include <string>

class QLocale;

/// Sums decimal numbers given as text, without any precision loss.
///
/// Only addition is needed here, so the "long arithmetic" boils down to adding
/// decimal strings in a column: a value is kept as a string of digits plus a
/// power of ten. The sum is exact for anything a dbms may print - a numeric of
/// arbitrary width, a float in exponential form - because neither double nor
/// int64 takes part in it. Hence no rounding, no overflow and no undefined
/// behaviour on NaN/Infinity, which are simply not numbers to sum.
class DecimalSum
{
public:
    /// Accepts an optionally signed decimal number with an optional fraction
    /// and an optional exponent: 1, -1.25, .5, 1e+30, 1E-7.
    /// Everything else, including NaN, Infinity and any trailing garbage,
    /// is rejected and left out of the sum.
    bool add(const QString &value);

    /// The amount of values accepted so far.
    int count() const noexcept { return _count; }
    /// The longest fraction seen, which is the scale the sum is printed with.
    int scale() const noexcept { return _scale; }
    bool isEmpty() const noexcept { return _count == 0; }

    /// Grouped digits and the decimal separator taken from the locale.
    QString toString(const QLocale &locale) const;
    /// Plain form: no grouping, a dot as the separator.
    QString toString() const;

private:
    QString text(const QString &decimalPoint,
                 const QString &groupSeparator,
                 int groupSize) const;

    // the value is _sign * _digits * 10^-_scale, where _digits holds decimal
    // digits, most significant first, with no leading zeros (empty means zero)
    int _sign = 1;
    std::string _digits;
    int _scale = 0;
    int _count = 0;
};

#endif // DECIMALSUM_H
