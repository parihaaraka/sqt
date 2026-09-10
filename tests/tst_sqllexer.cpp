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

    void reflowListIndentsOneStepPastTheOpeningLine()
    {
        const QString text = "create or replace function foo(a integer, b integer, c integer, d integer)\n"
                              "returns integer as $$ ... $$ language plpgsql;\n";
        const SqlListBounds b = _lexer.listBounds(text, -1);
        const QString rewritten = text.left(b.open + 1) + SqlLexer::reflowList(text, b, "\t") + text.mid(b.close);
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
        const QString once = text.left(b1.open + 1) + SqlLexer::reflowList(text, b1, "  ") + text.mid(b1.close);

        const SqlListBounds b2 = _lexer.listBounds(once, once.indexOf("a,"));
        const QString twice = once.left(b2.open + 1) + SqlLexer::reflowList(once, b2, "  ") + once.mid(b2.close);

        QCOMPARE(twice, once);
    }

    void reflowListOnAnEmptyBoundsIsAnEmptyString()
    {
        QCOMPARE(SqlLexer::reflowList("select a, b", SqlListBounds{}, "\t"), QString());
    }

    // ---- horizontalSpaceRun(): the leftover space/tab after a spliced list

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

    // The actual bug this exists to fix: splicing a bare list's replacement
    // used to leave the one space that separated it from what followed -
    // "... c from t" became "...c\n from t", not "...c\nfrom t".
    void splicingABareListDoesNotLeaveAStraySpaceBeforeWhatFollows()
    {
        const QString text = "select a, b, c from t";
        const int from = text.indexOf("a,");
        const int to = text.indexOf(" from"); // stops right before the space
        const SqlListBounds b = _lexer.listBoundsInRange(text, from, to);

        const QString replacement = SqlLexer::reflowList(text, b, "\t");
        const int spliceEnd = b.close + SqlLexer::horizontalSpaceRun(text, b.close);
        const QString rewritten = text.left(b.open + 1) + replacement + text.mid(spliceEnd);

        QCOMPARE(rewritten, QStringLiteral("select \n\ta,\n\tb,\n\tc\nfrom t"));
    }
};

QTEST_APPLESS_MAIN(TestSqlLexer)
#include "tst_sqllexer.moc"
