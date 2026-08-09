#include "decimalsum.h"
#include <QLocale>
#include <algorithm>

namespace
{

// The widest numeric any dbms prints stays far below this, while an input like
// 1e+2000000000 would otherwise ask for all the memory there is.
const int maxDigits = 100000;

inline bool isDigit(QChar c) noexcept
{
    // QChar::isDigit() also accepts Arabic-Indic and other national digits,
    // which have no place in a value printed by a dbms
    return c >= '0' && c <= '9';
}

/// digits are held most significant first, with no leading zeros
void trimLeadingZeros(std::string &d)
{
    const size_t nonZero = d.find_first_not_of('0');
    if (nonZero == std::string::npos)
        d.clear();
    else if (nonZero)
        d.erase(0, nonZero);
}

int cmpAbs(const std::string &a, const std::string &b)
{
    if (a.size() != b.size())
        return a.size() < b.size() ? -1 : 1;
    const int c = a.compare(b);
    return (c < 0 ? -1 : (c > 0 ? 1 : 0));
}

std::string addAbs(const std::string &a, const std::string &b)
{
    std::string res;
    res.reserve(std::max(a.size(), b.size()) + 1);
    int carry = 0;
    auto ia = a.rbegin(), ib = b.rbegin();
    while (ia != a.rend() || ib != b.rend() || carry)
    {
        int sum = carry;
        if (ia != a.rend())
            sum += *ia++ - '0';
        if (ib != b.rend())
            sum += *ib++ - '0';
        carry = sum / 10;
        res.push_back(static_cast<char>('0' + sum % 10));
    }
    std::reverse(res.begin(), res.end());
    return res;
}

/// a must not be less than b
std::string subAbs(const std::string &a, const std::string &b)
{
    std::string res;
    res.reserve(a.size());
    int borrow = 0;
    auto ia = a.rbegin(), ib = b.rbegin();
    while (ia != a.rend())
    {
        int diff = (*ia++ - '0') - borrow - (ib != b.rend() ? *ib++ - '0' : 0);
        borrow = (diff < 0 ? 1 : 0);
        if (diff < 0)
            diff += 10;
        res.push_back(static_cast<char>('0' + diff));
    }
    std::reverse(res.begin(), res.end());
    trimLeadingZeros(res);
    return res;
}

} // anonymous namespace

bool DecimalSum::add(const QString &value)
{
    const QString v = value.trimmed();
    int pos = 0;
    int sign = 1;
    if (pos < v.size() && (v[pos] == '+' || v[pos] == '-'))
        sign = (v[pos++] == '-' ? -1 : 1);

    std::string digits;
    int intLen = 0, fracLen = 0;
    while (pos < v.size() && isDigit(v[pos]))
    {
        digits.push_back(static_cast<char>(v[pos++].unicode()));
        ++intLen;
    }
    if (pos < v.size() && v[pos] == '.')
    {
        ++pos;
        while (pos < v.size() && isDigit(v[pos]))
        {
            digits.push_back(static_cast<char>(v[pos++].unicode()));
            ++fracLen;
        }
    }
    // no digits at all: text, an empty cell, NaN or Infinity
    if (!intLen && !fracLen)
        return false;

    int exp = 0;
    if (pos < v.size() && (v[pos] == 'e' || v[pos] == 'E'))
    {
        ++pos;
        int expSign = 1;
        if (pos < v.size() && (v[pos] == '+' || v[pos] == '-'))
            expSign = (v[pos++] == '-' ? -1 : 1);
        int expDigits = 0;
        while (pos < v.size() && isDigit(v[pos]))
        {
            if (++expDigits > 9)    // beyond any meaning, and int would overflow
                return false;
            exp = exp * 10 + (v[pos++].unicode() - '0');
        }
        if (!expDigits)
            return false;
        exp *= expSign;
    }
    // anything left is garbage: units, currency signs, a second dot
    if (pos != v.size())
        return false;

    int scale = fracLen - exp;
    if (scale < 0)  // the exponent outgrows the fraction, so the value is whole
    {
        if (-scale > maxDigits)
            return false;
        digits.append(static_cast<size_t>(-scale), '0');
        scale = 0;
    }
    if (scale > maxDigits || digits.size() > static_cast<size_t>(maxDigits))
        return false;

    trimLeadingZeros(digits);

    // both operands have to share the scale of the wider one
    if (scale > _scale)
    {
        if (!_digits.empty())
            _digits.append(static_cast<size_t>(scale - _scale), '0');
        _scale = scale;
    }
    else if (scale < _scale && !digits.empty())
        digits.append(static_cast<size_t>(_scale - scale), '0');

    if (sign == _sign)
        _digits = addAbs(_digits, digits);
    else if (cmpAbs(_digits, digits) >= 0)
        _digits = subAbs(_digits, digits);
    else
    {
        _digits = subAbs(digits, _digits);
        _sign = sign;
    }

    trimLeadingZeros(_digits);
    if (_digits.empty())    // a zero total is never negative
        _sign = 1;
    ++_count;
    return true;
}

QString DecimalSum::toString() const
{
    return text(QStringLiteral("."), QString(), 0);
}

QString DecimalSum::toString(const QLocale &locale) const
{
    const bool group = !(locale.numberOptions() & QLocale::OmitGroupSeparator);
    return text(QString(locale.decimalPoint()),
                group ? QString(locale.groupSeparator()) : QString(),
                3);
}

QString DecimalSum::text(const QString &decimalPoint,
                         const QString &groupSeparator,
                         int groupSize) const
{
    std::string d = (_digits.empty() ? std::string("0") : _digits);
    // a value below one needs the zeros the digits alone do not carry
    if (static_cast<int>(d.size()) <= _scale)
        d.insert(0, static_cast<size_t>(_scale) - d.size() + 1, '0');

    const int intLen = static_cast<int>(d.size()) - _scale;
    QString res;
    if (_sign < 0)
        res += QLatin1Char('-');
    for (int i = 0; i < intLen; ++i)
    {
        if (i && groupSize && !groupSeparator.isEmpty() && (intLen - i) % groupSize == 0)
            res += groupSeparator;
        res += QLatin1Char(d[static_cast<size_t>(i)]);
    }
    if (_scale)
    {
        res += decimalPoint;
        res += QString::fromLatin1(d.data() + intLen, _scale);
    }
    return res;
}
