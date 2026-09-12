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

/*!
 * \brief \a key, normalized to a Latin letter's Qt::Key_A..Key_Z when \a key
 *        itself is not one but the OS's own native virtual-key code says the
 *        physically-pressed key is.
 * \param key  a QKeyEvent's key(), as delivered by Qt
 * \param nativeVirtualKey  the same event's nativeVirtualKey()
 * \return \a key unchanged in the overwhelming majority of cases - only
 *         overridden when \a key is not already some Latin letter but
 *         \a nativeVirtualKey (on Windows only - elsewhere this is a no-op)
 *         falls in the 'A'..'Z' range
 *
 * Why this is needed at all: Ctrl+<letter> shortcuts that are not one of
 * Qt's own QKeySequence::StandardKey entries are usually matched by
 * comparing \c event->key() against a Qt::Key_<Letter> constant directly.
 * That is the *translated* key - what character the currently active
 * keyboard layout says this key produces - which works fine for any
 * Latin-alphabet layout, US or otherwise (a reordered one like AZERTY or
 * QWERTZ still produces *some* recognizable Latin letter, just not
 * necessarily the one printed on an equivalent US keyboard's version of that
 * physical key - Qt's own translation already gets that case right). It
 * silently breaks, though, for a layout with no Latin letters at all
 * (Cyrillic, Greek, Hebrew, Arabic...): \c key() then comes back as
 * something that is not any Qt::Key_A..Key_Z value, so the comparison never
 * matches and the shortcut simply never fires - not a crash, not a warning,
 * just quietly dead.
 *
 * Windows' own native virtual-key code, in contrast, stays pinned to the
 * physical key's position for the vast majority of non-Latin layouts (most
 * are still laid out on a QWERTY-shaped keyboard underneath, Cyrillic
 * ЙЦУКЕН included, and Windows keeps VK_A..VK_Z assigned to the same
 * physical keys as a US layout for exactly this kind of shortcut
 * compatibility) - so it is consulted as a fallback, but only as one: \a key
 * is trusted first and left alone whenever it already resolves to a Latin
 * letter, so a genuinely reordered Latin layout is not second-guessed by a
 * "physical position" interpretation that would only be right for it by
 * coincidence.
 *
 * Known, accepted gap - the same one upstream Qt itself does not close: a
 * fully reordered Latin layout (AZERTY swaps several letters outright) is
 * not something native-virtual-key matching fixes either, since Windows
 * *does* remap VK codes for those specific keys too. This only helps the
 * "no Latin letters on this layout at all" case, which is the one that was
 * actually reported as broken.
 */
int effectiveLetterKey(int key, quint32 nativeVirtualKey);

#endif // MISC_H
