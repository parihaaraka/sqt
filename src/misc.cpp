#include "misc.h"
#include "qdir.h"
#include <charconv>
#include <cstring>
#include <system_error>
#ifdef Q_OS_WIN
#include <QtCore/qnamespace.h>
#endif

QJsonDocument readJsonFile(const QString &path)
{
    QJsonDocument jdoc;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        auto text_data = file.readAll();
        if (!text_data.isEmpty())
        {
            QJsonParseError error;
            jdoc = QJsonDocument::fromJson(text_data, &error);
            if (error.error != QJsonParseError::NoError)
                throw error.errorString();
        }
        file.close();
    }
    return jdoc;
}

double parseDouble(const char *text, bool *ok)
{
    if (ok)
        *ok = false;
    if (!text)
        return 0;

    const char *first = text;
    while (*first == ' ' || *first == '\t' || *first == '\n' ||
           *first == '\r' || *first == '\f' || *first == '\v')
        ++first;

    // std::from_chars is the one conversion the standard defines as
    // locale-independent, which is the whole point here. It does not accept a
    // leading '+' (strtod does), so that is skipped by hand - but only for a
    // '+', since the sign of a negative number belongs to the number.
    const bool plus = (*first == '+');
    if (plus)
        ++first;

    const char *last = first + std::strlen(first);
    double value = 0;
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc() || res.ptr == first)
        return 0;   // nothing numeric here at all

    if (ok)
        *ok = true;
    return value;
}

int effectiveLetterKey(int key, quint32 nativeVirtualKey)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return key; // already a Latin letter - some Latin layout, trust it as-is

#ifdef Q_OS_WIN
    // Windows defines VK_A..VK_Z to equal the ASCII codes 'A'..'Z' - the same
    // numbers Qt::Key_A..Key_Z happen to use, which is what lets a bare
    // integer comparison work here without a lookup table.
    if (nativeVirtualKey >= 'A' && nativeVirtualKey <= 'Z')
        return Qt::Key_A + (static_cast<int>(nativeVirtualKey) - 'A');
#else
    Q_UNUSED(nativeVirtualKey);
#endif

    return key;
}
