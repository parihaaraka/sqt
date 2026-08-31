#ifndef MISC_H
#define MISC_H

#include "qjsondocument.h"

QJsonDocument readJsonFile(const QString &path);

/// Parses a double out of \a text regardless of the current locale, and of what
/// Qt has done to it: QApplication applies the system locale on startup, so on a
/// machine whose LC_NUMERIC uses a comma (ru_RU, de_DE, ...) the C library's
/// atof()/strtod() stop at the '.' of a value like "1.5" and return 1.
///
/// The values here are machine-readable text - what a dbms printed over the
/// wire, or a number inside a stylesheet - never something a user typed, so the
/// decimal separator is always '.' and a locale must not enter into it.
///
/// Lenient like strtod, which it replaces: leading whitespace and a leading '+'
/// are skipped, parsing stops at the first character that cannot belong to the
/// number ("9.5pt" gives 9.5), and \a ok reports whether a number was found at
/// all. The special forms postgres prints for a float - "NaN", "Infinity",
/// "-Infinity" - are understood, as they were by atof().
double parseDouble(const char *text, bool *ok = nullptr);

#endif // MISC_H
