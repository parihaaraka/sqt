#include "misc.h"
#include "qdir.h"
#include <charconv>
#include <cstring>
#include <system_error>

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
    const bool plus = (*first == '+');
    if (plus)
        ++first;
    const char *last = first + std::strlen(first);
    double value = 0;
    const auto res = std::from_chars(first, last, value);
    if (res.ec != std::errc() || res.ptr == first)
        return 0;

    if (ok)
        *ok = true;
    return value;
}

int effectiveShortcutKey(int key, quint32 nativeScanCode, quint32 nativeVirtualKey)
{
    // If Qt already translated the key to a Latin letter or to a punctuation
    // key, trust that result. This preserves reordered Latin layouts such as
    // AZERTY/QWERTZ instead of second-guessing them by physical position.
    if ((key >= Qt::Key_A && key <= Qt::Key_Z) ||
        (key >= Qt::Key_Exclam && key <= Qt::Key_AsciiTilde))
        return key;

    // Every one of Qt's *named* keys - arrows, Home/End, F1..F35, Escape, the
    // lot - lives at or above Qt::Key_Escape (0x01000000); every character a
    // layout can actually produce, Latin or not, is a Unicode code point below
    // it. AppEventHandler calls this for *every* key press application-wide,
    // not just Ctrl+<letter> ones, so without this guard a native code that
    // happens to coincide with an entry below - purely by numeric accident,
    // nothing here has ever looked at which key was pressed - silently
    // relabels Up, Left, F-keys and the rest as whatever letter or
    // punctuation mark that entry names. That is exactly how Ctrl+Up once
    // ended up opening the json viewer (Ctrl+J) instead of moving the caret:
    // arrow keys are not letters and were never meant to reach the tables
    // below at all.
    if (key >= Qt::Key_Escape)
        return key;

    // Below we deliberately do not branch on the build platform.
    // Instead every native value is matched by shape. This works because the
    // ranges genuinely do not overlap:
    //  - Linux evdev keycodes and Windows Scan Code Set 1 use the very same
    //    numbers for the keys handled here, and X11's XKB keycodes are those
    //    same evdev codes offset by a constant +8 (X11 reserves the first 8
    //    keycodes) - so one table below, tried at both the raw value and the
    //    value minus 8, covers evdev, XKB and Windows scan codes at once.
    //  - Windows OEM virtual-key codes live in 0xBA-0xDE.
    //  - Windows virtual-key codes for letters are plain ASCII 'A'-'Z'.
    //  - macOS Cocoa hardware key codes fall in 0-50, clear of both.
    //
    // No Q_OS_WIN/Q_OS_MACOS guard around any of this, on purpose: this file
    // is compiled once, on Linux, for tests/tst_keyboardshortcuts.cpp, which
    // is the only thing that exercises the Windows and macOS branches at all
    // - a #ifdef here would silently compile them out there and no CI would
    // ever run them for real.

    if (nativeScanCode)
    {
        static const struct { quint32 scan; int key; } punctMap[] = {
            {39, Qt::Key_Semicolon}, {13, Qt::Key_Equal},   {51, Qt::Key_Comma},
            {12, Qt::Key_Minus},     {52, Qt::Key_Period},  {53, Qt::Key_Slash},
            {41, Qt::Key_QuoteLeft}, {26, Qt::Key_BracketLeft},
            {27, Qt::Key_BracketRight}, {43, Qt::Key_Backslash},
            {40, Qt::Key_Apostrophe}
        };
        static const struct { quint32 scan; int key; } letterMap[] = {
            {30, Qt::Key_A}, {48, Qt::Key_B}, {46, Qt::Key_C},
            {32, Qt::Key_D}, {18, Qt::Key_E}, {33, Qt::Key_F},
            {34, Qt::Key_G}, {35, Qt::Key_H}, {23, Qt::Key_I},
            {36, Qt::Key_J}, {37, Qt::Key_K}, {38, Qt::Key_L},
            {50, Qt::Key_M}, {49, Qt::Key_N}, {24, Qt::Key_O},
            {25, Qt::Key_P}, {16, Qt::Key_Q}, {19, Qt::Key_R},
            {31, Qt::Key_S}, {20, Qt::Key_T}, {22, Qt::Key_U},
            {47, Qt::Key_V}, {17, Qt::Key_W}, {45, Qt::Key_X},
            {21, Qt::Key_Y}, {44, Qt::Key_Z}
        };

        const quint32 candidates[] = {
            nativeScanCode,
            nativeScanCode >= 8 ? nativeScanCode - 8 : nativeScanCode // XKB -> evdev
        };
        // Punctuation is checked - at both candidate values - before any
        // letter: some XKB codes collide numerically with an unrelated evdev
        // *letter* position (XKB semicolon 47 == evdev V, XKB equal 21 ==
        // evdev Y, and a few more), and a shortcut is far more likely to be
        // one of the punctuation keys this table exists for in the first
        // place than the specific colliding letter.
        for (quint32 code : candidates)
            for (const auto &entry : punctMap)
                if (code == entry.scan)
                    return entry.key;
        for (quint32 code : candidates)
            for (const auto &entry : letterMap)
                if (code == entry.scan)
                    return entry.key;
    }

    if (nativeVirtualKey)
    {
        // Windows virtual-key codes for the OEM punctuation keys are layout
        // independent. In particular, on a Russian layout VK_OEM_COMMA still
        // identifies the physical US comma key which Qt translates as Cyrillic Б.
        switch (nativeVirtualKey)
        {
        case 0xBA: return Qt::Key_Semicolon;    // VK_OEM_1
        case 0xBB: return Qt::Key_Equal;        // VK_OEM_PLUS
        case 0xBC: return Qt::Key_Comma;        // VK_OEM_COMMA
        case 0xBD: return Qt::Key_Minus;        // VK_OEM_MINUS
        case 0xBE: return Qt::Key_Period;       // VK_OEM_PERIOD
        case 0xBF: return Qt::Key_Slash;        // VK_OEM_2
        case 0xC0: return Qt::Key_QuoteLeft;    // VK_OEM_3
        case 0xDB: return Qt::Key_BracketLeft;  // VK_OEM_4
        case 0xDC: return Qt::Key_Backslash;    // VK_OEM_5
        case 0xDD: return Qt::Key_BracketRight; // VK_OEM_6
        case 0xDE: return Qt::Key_Apostrophe;   // VK_OEM_7
        default:
            break;
        }

        if (nativeVirtualKey >= 'A' && nativeVirtualKey <= 'Z')
            return Qt::Key_A + (static_cast<int>(nativeVirtualKey) - 'A');

        // macOS nativeVirtualKey is the Cocoa hardware key code. These are
        // the physical positions used by Apple's US ANSI keyboard.
        switch (nativeVirtualKey)
        {
        case 24: return Qt::Key_Equal;
        case 27: return Qt::Key_Minus;
        case 43: return Qt::Key_Comma;
        case 47: return Qt::Key_Period;
        case 44: return Qt::Key_Slash;
        case 50: return Qt::Key_QuoteLeft;
        case 33: return Qt::Key_BracketLeft;
        case 30: return Qt::Key_BracketRight;
        case 39: return Qt::Key_Apostrophe;
        case 0:  return Qt::Key_A;
        case 11: return Qt::Key_B;
        case 8:  return Qt::Key_C;
        case 2:  return Qt::Key_D;
        case 14: return Qt::Key_E;
        case 3:  return Qt::Key_F;
        case 5:  return Qt::Key_G;
        case 4:  return Qt::Key_H;
        case 34: return Qt::Key_I;
        case 38: return Qt::Key_J;
        case 40: return Qt::Key_K;
        case 37: return Qt::Key_L;
        case 46: return Qt::Key_M;
        case 45: return Qt::Key_N;
        case 31: return Qt::Key_O;
        case 35: return Qt::Key_P;
        case 12: return Qt::Key_Q;
        case 15: return Qt::Key_R;
        case 1:  return Qt::Key_S;
        case 17: return Qt::Key_T;
        case 32: return Qt::Key_U;
        case 9:  return Qt::Key_V;
        case 13: return Qt::Key_W;
        case 7:  return Qt::Key_X;
        case 16: return Qt::Key_Y;
        case 6:  return Qt::Key_Z;
        default:
            break;
        }
    }

    return key;
}
