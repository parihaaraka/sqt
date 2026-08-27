#include <QtTest>
#include <QTextDocument>
#include "wordnav.h"

// Ctrl+Left / Ctrl+Right stops. The interesting part is rule 2 from wordnav.h -
// a jump stays inside the caret's line - which is what the editor used to get
// wrong: Ctrl+Left from the first non-blank character of an indented line
// skipped the indentation and the line break in one go.
class TestWordNav : public QObject
{
    Q_OBJECT

private:
    // The '|' in the argument marks the caret; the rest is the document.
    // Returns the resulting position, again as an offset into the text.
    static int jump(const QString &textWithCaret, bool forward)
    {
        int caret = textWithCaret.indexOf(QLatin1Char('|'));
        Q_ASSERT(caret >= 0);
        QString text = textWithCaret;
        text.remove(caret, 1);

        QTextDocument doc;
        doc.setPlainText(text);
        return forward ? WordNav::nextBoundary(&doc, caret)
                       : WordNav::previousBoundary(&doc, caret);
    }

    static int back(const QString &t) { return jump(t, false); }
    static int fwd(const QString &t)  { return jump(t, true); }

private slots:
    // ---- rule 2: the line's own edge is a stop -----------------------

    void backFromIndentedTextStopsAtLineStart()
    {
        // "select 1\n   from t", caret before "from": lands at column 0 of
        // its own line (offset 9), not inside "select 1" above.
        QCOMPARE(back("select 1\n   |from t"), 9);
    }

    void backFromLineStartCrossesTheBreak()
    {
        // Pressed again from that column 0, it finally goes up - to the
        // start of the last word of the line above ("1" at offset 7).
        QCOMPARE(back("select 1\n|   from t"), 7);
    }

    void backFromMidWordStaysInTheWord()
    {
        QCOMPARE(back("select fr|om"), 7);
    }

    void backFromTextStartIsClamped()
    {
        QCOMPARE(back("|select"), 0);
    }

    void backAcrossEmptyLineStopsOnIt()
    {
        // An empty line is a stop of its own (VS Code does the same): from
        // the start of "b" the jump lands on the blank line at offset 2,
        // not on "a".
        QCOMPARE(back("a\n\n|b"), 2);
    }

    void forwardFromLineEndCrossesTheBreak()
    {
        // Caret at the end of "select" (offset 6), which is also the end of
        // the line: the jump crosses the break and stops at the end of the
        // first word on the next line ("from", offset 7+4=11).
        QCOMPARE(fwd("select|\nfrom t"), 11);
    }

    void forwardStopsAtLineEnd()
    {
        // From inside the last word of a line, the stop is that line's end
        // (offset 8), not something on the line below.
        QCOMPARE(fwd("select 1|1\n   from"), 9);
    }

    void forwardOverTrailingBlanksStopsAtLineEnd()
    {
        // Trailing whitespace is crossed, but the line's end is still where
        // it stops - "a   \nb", caret after "a", stops at offset 4.
        QCOMPARE(fwd("a|   \nb"), 4);
    }

    // ---- rule 1: whitespace is crossed, never landed in --------------

    void forwardLandsAtWordEndNotAfterTheSpace()
    {
        // "select from", caret at 0 -> end of "select" (6), so
        // Ctrl+Shift+Right selecting a word does not pull in the space.
        QCOMPARE(fwd("|select from"), 6);
    }

    void forwardFromWordEndCrossesSpaceToNextWordEnd()
    {
        QCOMPARE(fwd("select| from"), 11);
    }

    void backFromWordStartCrossesSpaceToPreviousWordStart()
    {
        QCOMPARE(back("select |from"), 0);
    }

    // ---- the "glued separator" exception -----------------------------

    void forwardMergesLoneSeparatorIntoFollowingWord()
    {
        // "qwe.rty" is two Ctrl+Right presses, not three: from the end of
        // "qwe" the '.' is glue and the stop is the end of "rty".
        QCOMPARE(fwd("qwe|.rty"), 7);
    }

    void backMergesLoneSeparatorIntoPrecedingWord()
    {
        QCOMPARE(back("qwe.rty|"), 4);
    }

    void forwardKeepsRunOfSeparatorsAsItsOwnStop()
    {
        // A *run* of separators still gets its own stop: "a...b", caret
        // after "a", stops after "..." (offset 4).
        QCOMPARE(fwd("a|...b"), 4);
    }

    void forwardKeepsSeparatorBoundedByWhitespace()
    {
        // "a = b": the '=' has whitespace on the far side, so it is not
        // glue - the stop is right after it (offset 3).
        QCOMPARE(fwd("a| = b"), 3);
    }

    // ---- document edges ----------------------------------------------

    void forwardAtDocumentEndIsClamped()
    {
        QCOMPARE(fwd("select|"), 6);
    }

    void emptyDocumentGoesNowhere()
    {
        QCOMPARE(fwd("|"), 0);
        QCOMPARE(back("|"), 0);
    }

    void backFromLeadingIndentOfFirstLineReachesZero()
    {
        // Nothing above to cross into: the whole leading run of blanks is
        // walked and the stop is the very start of the document.
        QCOMPARE(back("   |select"), 0);
    }
};

QTEST_APPLESS_MAIN(TestWordNav)
#include "tst_wordnav.moc"
