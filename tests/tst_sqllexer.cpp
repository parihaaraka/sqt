#include <QtTest>
#include "sqllexer.h"

/// What "Split list into lines" (the editor's context menu item, and the
/// function/procedure DDL auto-format) relies on: finding a comma-separated
/// list's bounds - by bracket search or, for a bare list like a select's
/// field list, by an explicit range - and rewriting it one item per line.
/// None of this needs a real hl.conf's keyword dictionaries (listBounds()
/// only cares about brackets, commas and scanLine()'s claimed spans), so an
/// empty QJsonDocument is a perfectly good lexer for every case here.
class TestSqlLexer : public QObject
{
    Q_OBJECT

    SqlLexer _lexer{QJsonDocument()};

private slots:
    // ---- listBounds(): bracket search ---------------------------------

    // The tricky bit: a nested call and a literal that itself contains a
    // comma must not be mistaken for the outer list's own separators.
    void innermostBracketIsFoundAtTheCaret()
    {
        const QString text = "select string_agg(left(t.f1, 5), ',' order by substring(t.f2, 5)) from t";
        const int pos = text.indexOf("t.f1"); // inside left(...)
        const SqlListBounds b = _lexer.listBounds(text, pos);
        QCOMPARE(text.mid(b.open, b.close - b.open + 1), QStringLiteral("(t.f1, 5)"));
        QCOMPARE(b.separators.size(), 1);
    }

    void outerBracketSkipsTheNestedCommaAndTheLiteralOne()
    {
        const QString text = "select string_agg(left(t.f1, 5), ',' order by substring(t.f2, 5)) from t";
        const int pos = text.indexOf("order by") + 2; // still inside string_agg(...)
        const SqlListBounds b = _lexer.listBounds(text, pos);
        QCOMPARE(text.mid(b.open, b.close - b.open + 1),
                 QStringLiteral("(left(t.f1, 5), ',' order by substring(t.f2, 5))"));
        // Exactly one separator: right after left(...) closes. Not the one
        // inside left(...) itself, not the one inside the ',' literal.
        QCOMPARE(b.separators.size(), 1);
    }

    // A bare list - no enclosing bracket at all - is out of scope for
    // listBounds() on purpose; see listBoundsInRange() for how it is reached.
    void bareListIsNotFound()
    {
        const QString text = "select a, b, c from t";
        const SqlListBounds b = _lexer.listBounds(text, text.indexOf(" b"));
        QCOMPARE(b.close, -1);
    }

    // pos < 0: the first top-level bracket in the whole text - a routine's
    // own parameter list, skipping a leading comment even when the comment
    // itself contains parens (the commented-out `DROP FUNCTION` some content
    // scripts prepend).
    void negativePositionFindsTheFirstTopLevelBracket()
    {
        const QString text =
            "/* drop function foo(a int, b int); */\n"
            "create or replace function foo(a integer, b integer, c integer, d integer)\n"
            "returns integer as $$ begin return a + b + c + d; end; $$ language plpgsql;\n";
        const SqlListBounds b = _lexer.listBounds(text, -1);
        QCOMPARE(text.mid(b.open, b.close - b.open + 1),
                 QStringLiteral("(a integer, b integer, c integer, d integer)"));
        QCOMPARE(b.separators.size(), 3);
    }

    void singleParameterHasNoSeparators()
    {
        const QString text = "select foo(a) from t";
        const SqlListBounds b = _lexer.listBounds(text, text.indexOf("a)"));
        QCOMPARE(b.close, text.indexOf(")"));
        QVERIFY(b.separators.isEmpty());
    }

    // ---- listBoundsInRange(): an explicit range stands in for the search -

    void bareListIsFoundWithinAnExplicitRange()
    {
        const QString text = "select a, string_agg(x, ',' order by y), c from t";
        const int from = text.indexOf("a,");
        const int to = text.indexOf(" from");
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        // 2 separators: after 'a' and after string_agg(...) - not the comma
        // inside string_agg(...)'s own args, not the one in the ',' literal.
        QCOMPARE(b.separators.size(), 2);
        QCOMPARE(b.open, from - 1);
        QCOMPARE(b.close, to);
    }

    // The open == -1 edge case: a range starting at position 0 of the text is
    // a legitimate result, not the "nothing found" sentinel - close is what
    // callers are expected to check instead (see the struct's own docs).
    void rangeStartingAtZeroIsNotMistakenForNotFound()
    {
        const QString text = "a, b, c";
        const SqlListBounds b = _lexer.listBoundsInRange(text, 0, text.length());
        QCOMPARE(b.open, -1);
        QCOMPARE(b.close, text.length());
        QCOMPARE(b.separators.size(), 2);
    }

    // A selection that is only part of an outer bracketed list: what
    // encloses the range is none of its business, only what is inside it.
    void aSublistOfABracketedListIgnoresWhatEnclosesIt()
    {
        const QString text = "select foo(a, b, c, d) from t";
        const int from = text.indexOf("a, b, c");
        const int to = from + QStringLiteral("a, b, c").length();
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        QCOMPARE(b.separators.size(), 2);
    }

    // No top-level comma in the range at all - close is still a legitimate
    // (non-sentinel) value, separators is just empty; callers check the
    // latter to decide there is nothing worth "splitting".
    void aRangeWithNoCommaHasNoSeparatorsButIsStillFound()
    {
        const QString text = "select foo(a, b) from t";
        const int from = text.indexOf("foo");
        const int to = from + 3;
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        QVERIFY(b.close >= 0);
        QVERIFY(b.separators.isEmpty());
    }

    void anInvalidRangeIsNotFound()
    {
        const QString text = "a, b, c";
        QCOMPARE(_lexer.listBoundsInRange(text, 5, 2).close, -1);   // to < from
        QCOMPARE(_lexer.listBoundsInRange(text, -1, 3).close, -1);  // from < 0
        QCOMPARE(_lexer.listBoundsInRange(text, 0, 100).close, -1); // to > length
    }

    // ---- reflowList(): the actual rewrite ------------------------------

    static QString splice(const QString &text, const SqlListReflow &r)
    {
        return text.left(r.start) + r.replacement + text.mid(r.end);
    }

    void reflowListIndentsOneStepPastTheOpeningLine()
    {
        const QString text = "create or replace function foo(a integer, b integer, c integer, d integer)\n"
                              "returns integer as $$ ... $$ language plpgsql;\n";
        const SqlListBounds b = _lexer.listBounds(text, -1);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten,
                 QStringLiteral("create or replace function foo(\n"
                                 "\ta integer,\n"
                                 "\tb integer,\n"
                                 "\tc integer,\n"
                                 "\td integer\n"
                                 ")\n"
                                 "returns integer as $$ ... $$ language plpgsql;\n"));
    }

    // Running it again on its own output must not pile up blank lines or
    // drift indentation - each item is trimmed before being placed back.
    void reflowListIsIdempotent()
    {
        const QString text = "select foo(a, b, c, d) from t";
        const SqlListBounds b1 = _lexer.listBounds(text, text.indexOf("a,"));
        const QString once = splice(text, SqlLexer::reflowList(text, b1, "  "));

        const SqlListBounds b2 = _lexer.listBounds(once, once.indexOf("a,"));
        const QString twice = splice(once, SqlLexer::reflowList(once, b2, "  "));

        QCOMPARE(twice, once);
    }

    void reflowListOnAnEmptyBoundsIsEmpty()
    {
        const SqlListReflow r = SqlLexer::reflowList("select a, b", SqlListBounds{}, "\t");
        QCOMPARE(r.start, -1);
        QCOMPARE(r.end, -1);
        QVERIFY(r.replacement.isEmpty());
    }

    // The actual bug this exists to fix: splicing a bare list's replacement
    // used to leave the one space that separated it from what followed -
    // "... c from t" became "...c\n from t", not "...c\nfrom t".
    void reflowListDoesNotLeaveAStraySpaceBeforeWhatFollows()
    {
        const QString text = "select a, b, c from t";
        const int from = text.indexOf("a,");
        const int to = text.indexOf(" from"); // stops right before the space
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("select\n\ta,\n\tb,\n\tc\nfrom t"));
    }

    // The three scenarios from the person who asked for this: selecting more
    // than just the list should not invent a blank line or an indentation
    // level that was not there to begin with - a break is only inserted (and
    // the rest of the list indented to match) where the first item actually
    // needs pushing onto a fresh line at all.

    void splittingTheWholeQueryOnlyBreaksAtTheComma()
    {
        const QString text = "select a, b\nfrom t";
        const SqlListBounds b = _lexer.listBoundsInRange(text, 0, text.length());
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("select a,\nb\nfrom t"));
    }

    void splittingJustTheFirstLineLeavesItFlush()
    {
        const QString text = "select a, b\nfrom t";
        const int to = text.indexOf('\n');
        const SqlListBounds b = _lexer.listBoundsInRange(text, 0, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("select a,\nb\nfrom t"));
    }

    void splittingPreciselyTheColumnListIndentsBothItems()
    {
        const QString text = "select a, b\nfrom t";
        const int from = text.indexOf("a,");
        const int to = text.indexOf('\n');
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("select\n\ta,\n\tb\nfrom t"));
    }

    // A second round of feedback, this time with the source itself already
    // indented, and selections that stop short of a trailing ",c" still
    // belonging to the very same list. Source for all four:
    //   \t\tselect a,b,c
    //   \t\tfrom t
    // (only the first two use "a,b" instead of "a,b,c" - see each case).

    // Whole two lines selected: the pre-existing "\t\t" indentation must
    // survive (the homeTextStart scan used to be capped at bounds.open,
    // which is -1 for a selection starting at position 0 of the whole text -
    // so it never looked past that and reported no indentation at all), and
    // a trailing newline swept into the selection along with "from t" must
    // not collapse into "from t" losing its own line, let alone eating a
    // blank line further down.
    void wholeIndentedLinesKeepTheirIndentAndTrailingBlankLine()
    {
        const QString text = "\t\tselect a,b\n\t\tfrom t\n\nselect * from t2\n";
        const int to = text.indexOf("from t") + QStringLiteral("from t").length();
        const SqlListBounds b = _lexer.listBoundsInRange(text, 0, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten,
                 QStringLiteral("\t\tselect a,\n\t\tb\n\t\tfrom t\n\nselect * from t2\n"));
    }

    // Same idea with the selection including the line's own trailing '\n' -
    // must come out identical to the case above rather than losing the blank
    // line that follows, regardless of exactly where the selection's own
    // edge happened to land.
    void aTrailingNewlineSweptIntoTheSelectionDoesNotEatTheBlankLineAfterIt()
    {
        const QString text = "\t\tselect a,b\n\t\tfrom t\n\nselect * from t2\n";
        const int to = text.indexOf("from t") + QStringLiteral("from t").length() + 1; // +1: the '\n'
        const SqlListBounds b = _lexer.listBoundsInRange(text, 0, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten,
                 QStringLiteral("\t\tselect a,\n\t\tb\n\t\tfrom t\n\nselect * from t2\n"));
    }

    // "select a,b" selected out of "\t\tselect a,b,c" (",c" is not part of the
    // selection): the trailing ",c" is the rest of the very same list, one
    // character past where the selection ends - it belongs right after "b",
    // not walled off behind a break of its own the way genuinely different
    // content (like "from t" above) would be.
    void aCommaRightPastTheSelectionJoinsTheLastItemInstead()
    {
        const QString text = "\t\tselect a,b,c\n\t\tfrom t";
        const int from = text.indexOf("select");
        const int to = text.indexOf(",c");
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("\t\tselect a,\n\t\tb,c\n\t\tfrom t"));
    }

    // Same source, "a,b" selected this time (not "select a,b"): the first
    // item now does get pushed onto its own line (real code - "select " -
    // still precedes it), so the rest of the list is indented one step past
    // "select"'s own line rather than staying level with it - but ",c" still
    // joins "b" rather than getting a break of its own.
    void aLeadingKeywordEarnsAFreshLineButATrailingCommaStillDoesNot()
    {
        const QString text = "\t\tselect a,b,c\n\t\tfrom t";
        const int from = text.indexOf("a,b");
        const int to = text.indexOf(",c");
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("\t\tselect\n\t\t\ta,\n\t\t\tb,c\n\t\tfrom t"));
    }

    // The mirror image of the previous two: a selection starting right after
    // a comma (the rest of the list continuing just *before* it, this time)
    // is treated the same way on that side.
    // A comma right before the selection does NOT get the same "already
    // fine" treatment as one right after it (see reflowList()'s own docs for
    // why): "b" still gets pushed onto its own fresh line here, "a," is left
    // exactly as it was rather than being treated as already being in the
    // right shape just because it happens to end in a comma too.
    void aCommaRightBeforeTheSelectionStillGetsAFreshLine()
    {
        const QString text = "select a, b, c from t";
        const int from = text.indexOf("b,");
        const int to = text.indexOf(" from");
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("select a,\n\tb,\n\tc\nfrom t"));
    }

    // The exact scenario that motivated the asymmetry above, with the source
    // itself indented: selecting "b,c" out of "\t\tselect a,b,c\n\t\tfrom t"
    // must NOT glue "b" after "a," on the same line the way a trailing
    // ",c" past the selection would have glued onto "b" instead - it gets
    // pushed onto its own line, indented one step past "select"'s own line,
    // by the same reasoning as selecting "a,b,c" whole would have.
    void aCommaRightBeforeAnIndentedSelectionStillGetsAFreshLine()
    {
        const QString text = "\t\tselect a,b,c\n\t\tfrom t";
        const int from = text.indexOf("b,c");
        const int to = from + QStringLiteral("b,c").length();
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("\t\tselect a,\n\t\t\tb,\n\t\t\tc\n\t\tfrom t"));
    }

    // A trailing comma *inside* the selection ("a,b," rather than "a,b") must
    // not turn into a blank line: once trimmed it is an empty last item, and
    // that is folded into the same "a comma right past the edge is fine, no
    // break needed" handling that a comma just *outside* the selection gets,
    // rather than being kept as a genuinely empty item to render as one.
    void aTrailingCommaInsideTheSelectionDoesNotProduceABlankLine()
    {
        const QString text = "\t\tselect a,b,c\n\t\tfrom t";
        const int from = text.indexOf("a,b,c");
        const int to = from + QStringLiteral("a,b,").length(); // "a,b," - trailing comma included
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);
        const QString rewritten = splice(text, SqlLexer::reflowList(text, b, "\t"));
        QCOMPARE(rewritten, QStringLiteral("\t\tselect\n\t\t\ta,\n\t\t\tb,c\n\t\tfrom t"));
    }

    // ---- horizontalSpaceRun(): still a useful scan on its own -----------

    void horizontalSpaceRunCoversSpacesAndTabsOnly()
    {
        const QString text = "a  \t b";
        // positions 1..4 are ' ', ' ', '\t', ' ' - stops at 'b' (index 5)
        QCOMPARE(SqlLexer::horizontalSpaceRun(text, 1), 4);
        QCOMPARE(SqlLexer::horizontalSpaceRun(text, 0), 0); // text.at(0) == 'a'
        QCOMPARE(SqlLexer::horizontalSpaceRun(text, text.length()), 0); // past the end
    }

    void horizontalSpaceRunIsZeroRightAfterAClosingBracket()
    {
        const QString text = "select foo(a, b, c, d) from t";
        const SqlListBounds b = _lexer.listBounds(text, text.indexOf("a,"));
        // bounds.close sits on ')' itself, not on whitespace - nothing to
        // absorb there, unlike the bare-list/selection case.
        QCOMPARE(SqlLexer::horizontalSpaceRun(text, b.close), 0);
    }
};

QTEST_APPLESS_MAIN(TestSqlLexer)
#include "tst_sqllexer.moc"
