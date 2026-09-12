#ifndef MISC_H
#define MISC_H
#include "qjsondocument.h"

QJsonDocument readJsonFile(const QString &path);
double parseDouble(const char *text, bool *ok = nullptr);

/*!
 * Returns the Qt key which represents the physical key intended by a shortcut.
 *
 * For ordinary Latin layouts the translated Qt key is deliberately trusted.
 * For non-Latin layouts Qt may translate a physical shortcut key (for example
 * the US comma key) to a Cyrillic/Greek/etc. key. Native key information is
 * then used as a fallback to recover the physical key.
 *
 * The native values are platform dependent; only the platforms for which Qt
 * exposes a stable native key/scancode are handled here. A zero native value
 * simply leaves \a key unchanged.
 */
int effectiveShortcutKey(int key, quint32 nativeScanCode, quint32 nativeVirtualKey);

/* Compatibility wrapper for the existing Windows-only letter use sites. */
inline int effectiveLetterKey(int key, quint32 nativeVirtualKey)
{
    return effectiveShortcutKey(key, 0, nativeVirtualKey);
}

#endif // MISC_H
