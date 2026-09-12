#include <QtTest>

#include "misc.h"

class KeyboardShortcutsTest : public QObject
{
    Q_OBJECT

private slots:
    void windowsLatinLetters();
    void windowsCyrillicLetters();
    void windowsPunctuation();
    void x11CyrillicPunctuation();
    void linuxEvdevPunctuation();
    void alreadyTranslatedKeysWin();
};

void KeyboardShortcutsTest::windowsLatinLetters()
{
    QCOMPARE(effectiveShortcutKey(Qt::Key_D, 0, 'D'), Qt::Key_D);
    QCOMPARE(effectiveShortcutKey(Qt::Key_Z, 0, 'Z'), Qt::Key_Z);
}

void KeyboardShortcutsTest::windowsCyrillicLetters()
{
    // Qt::Key_B is what a Cyrillic layout may produce for the physical US B
    // position. VK_D identifies the physical D key independently of the layout.
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'д'), 0, 'D'), Qt::Key_D);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'я'), 0, 'Z'), Qt::Key_Z);
}

void KeyboardShortcutsTest::windowsPunctuation()
{
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'б'), 0, 0xBC), Qt::Key_Comma);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ю'), 0, 0xBE), Qt::Key_Period);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ж'), 0, 0xBA), Qt::Key_Semicolon);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'э'), 0, 0xDE), Qt::Key_Apostrophe);
}

void KeyboardShortcutsTest::x11CyrillicPunctuation()
{
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'б'), 59, 0), Qt::Key_Comma);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ю'), 60, 0), Qt::Key_Period);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ж'), 47, 0), Qt::Key_Semicolon);
}

void KeyboardShortcutsTest::linuxEvdevPunctuation()
{
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'б'), 51, 0), Qt::Key_Comma);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ю'), 52, 0), Qt::Key_Period);
    QCOMPARE(effectiveShortcutKey(static_cast<int>(u'ж'), 39, 0), Qt::Key_Semicolon);
}

void KeyboardShortcutsTest::alreadyTranslatedKeysWin()
{
    // Do not turn a deliberately reordered Latin layout into a US layout.
    QCOMPARE(effectiveShortcutKey(Qt::Key_Q, 0, 'Q'), Qt::Key_Q);
    QCOMPARE(effectiveShortcutKey(Qt::Key_Comma, 0, 0xBC), Qt::Key_Comma);
    QCOMPARE(effectiveShortcutKey(Qt::Key_Period, 0, 0xBE), Qt::Key_Period);
}

QTEST_MAIN(KeyboardShortcutsTest)
#include "tst_keyboardshortcuts.moc"
