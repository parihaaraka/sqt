#ifndef SQLLEXER_H
#define SQLLEXER_H

#include <QHash>
#include <QSet>
#include <QString>
#include <QPair>
#include <QVector>
#include <functional>
#include <memory>

class QJsonDocument;
class DbConnection;

/// A comma-separated list: a function/procedure argument list, an IN (...) or
/// VALUES (...) list, a column list and so on - or, via listBoundsInRange(),
/// an arbitrary selection the person made themselves instead of one found by
/// bracket search, in which case \a open/\a close bound the selection rather
/// than a literal '('/')' pair.
/// \see SqlLexer::listBounds(), SqlLexer::listBoundsInRange()
struct SqlListBounds
{
    /// One before the list's first item - \a close - open - 1 is the item
    /// area's length. For a bracket-search result this is the '(' itself; for
    /// a range-based one, \a from - 1 (which is legitimately -1 when the
    /// range starts at the very beginning of the text - not a "nothing found"
    /// case; see \a close for the sentinel that is).
    int open = -1;
    /// \b close < 0, unlike \a open, always and only means "nothing found" -
    /// check this to tell a genuine miss apart from a range-based result that
    /// happens to start at position 0.
    int close = -1;
    QVector<int> separators; ///< positions of the top-level ',' in between
};

/// What SqlLexer::reflowList() computed - a splice, not just a fragment: the
/// list's own bounds are not necessarily what should actually be erased (see
/// reflowList()'s own docs on absorbing adjacent horizontal whitespace), so
/// callers are handed the range to use rather than left to work it out a
/// second time themselves.
struct SqlListReflow
{
    int start = -1;   ///< \c text[start, end) is what \a replacement replaces
    int end = -1;
    QString replacement;
};

/*!
 * \brief Dictionary-driven sql scanner shared by the highlighter and the
 *        keyword case folder.
 *
 * The scanner is the state machine originally living inside
 * SqlSyntaxHighlighter::highlightBlock(). It knows nothing about text formats:
 * it just reports spans of interest (literals, comments, quoted identifiers,
 * numbers, keywords and functions) through a callback, so both the painter and
 * the case folder may rely on the same (adjustable, per-dbms) dictionaries.
 *
 * Just like the highlighter it used to be a part of, the scanner works line by
 * line and carries an opaque state between the lines.
 */
class SqlLexer
{
public:
    enum class Token
    {
        Literal,        ///< 'literal'
        DoubleQuoted,   ///< "identifier"
        BracketQuoted,  ///< [identifier] (tsql)
        CommentBlock,   ///< /* comment */ (nestable)
        CommentLine,    ///< -- comment
        Number,
        Variable,       ///< tsql-like @variable
        Function,       ///< dictionary word followed by '('
        Keyword,        ///< dictionary word/phrase (\see Span::group)
        DollarQuoted,   ///< $tag$ body $tag$ (\see scanLine's dollarTag)
        MixedEncoding   ///< ascii and non-ascii characters within single word
    };

    struct Span
    {
        int start;
        int length;
        Token token;
        /// ordinal of the `keyword` partition of hl.conf (Token::Keyword only)
        int group;
    };

    /// state to start scanning of a standalone piece of text with
    static const int InitialState = 0xFF;

    explicit SqlLexer(const QJsonDocument &settings);

    /// amount of the `keyword` partitions found in hl.conf
    int keywordGroupCount() const { return _keywordGroupCount; }
    bool isKeyword(const QString &word) const;

    /// Whether this dbms allows the script to be cut into statements (hl.conf's
    /// `statement_split`), i.e. whether statementBounds() can answer at all.
    /// Worth asking before offering the user a "run the statement under the
    /// caret" command.
    bool canSplitStatements() const { return _statementSplit; }

    /*!
     * \brief Scan single line of text.
     * \param text line to scan (must not contain line breaks)
     * \param state state returned by the previous call (\see InitialState)
     * \param report callback to receive the spans found
     * \param dollarTag in/out dollar quoting tag (nullptr disables the feature)
     * \return state to continue scanning of the next line with
     *
     * Dollar quoting is opt-in to keep the highlighting of the routine bodies
     * (and of any other dollar quoted text) as detailed as it always was: the
     * highlighter passes nullptr and gets the body scanned as a plain sql,
     * while the case folder provides the tag holder to get the body reported
     * as a single opaque Token::DollarQuoted span to leave it untouched.
     */
    int scanLine(const QString &text,
                 int state,
                 const std::function<void(const Span &)> &report,
                 QString *dollarTag = nullptr) const;

    /*!
     * \brief Lowercase every keyword/function of the script.
     *
     * Nothing but the dictionary words gets modified: literals, comments,
     * quoted identifiers and dollar quoted bodies are copied verbatim. The
     * text length stays intact, so the worst case is a keyword left as is.
     */
    QString foldKeywords(const QString &script) const;

    /*!
     * \brief Bounds of the "current" top-level statement.
     * \param text  the whole script
     * \param pos   caret position within `text`
     * \return [start, end) of the statement at/after `pos`, trimmed of
     *         surrounding whitespace, or {-1, -1} where the split is not
     *         enabled for this dbms (\see canSplitStatements).
     *         If `pos` sits in the trailing part of the script with no more
     *         separators ahead, the range reaches text.length().
     *
     * The separator is ';' - the only one any server actually understands
     * (`GO`, `/`, `DELIMITER //` and the like are inventions of the various
     * command line clients). A "top-level" one is an occurrence which
     * scanLine() would leave unclaimed: not part of a '...'/"..." literal, a
     * [bracketed] or $tag$...$tag$ quoted body, or a comment. Dollar quoting
     * is always engaged here (regardless of dbms), because a bare '$' pair
     * never legitimately occurs in any dialect's plain SQL - so this is
     * exactly what lets a `DO $$ ... $$;` block, including any nested
     * single-quoted strings or differently-tagged $sub$...$sub$ literals
     * inside its plpgsql body, be treated as one statement, without a dbms
     * check.
     *
     * Whether the split is allowed at all is a dbms property, because ';' is
     * not a statement boundary everywhere: in T-SQL the one inside a
     * `CREATE PROCEDURE ... BEGIN ... END` body merely separates the
     * statements of the *body*, and cutting there would send the server a
     * fragment - or, worse, a fragment that is valid on its own and silently
     * runs. Such a dbms leaves `statement_split` out and gets {-1, -1}, so
     * the feature offers itself only where it can be trusted.
     *
     * Same limitations as scanLine()'s dollar-tag search: the closing tag is
     * matched by plain substring, exactly as the server does, so it is
     * fooled only by the same edge cases the server itself would reject.
     */
    QPair<int, int> statementBounds(const QString &text, int pos) const;

    /*!
     * \brief Bounds of the parenthesized, comma-separated list \a pos sits
     *        inside.
     * \param text  the whole script
     * \param pos   caret position within \a text; a negative value asks for
     *              the *first* top-level list in the whole text instead (see
     *              below), and \a pos itself is then ignored
     * \return the list's bounds, or \c SqlListBounds::open == -1 if none was
     *         found
     *
     * The *innermost* enclosing pair is reported: a caret inside
     * `left(t.f1, 5)` of `string_agg(left(t.f1, 5), ',' order by ...)` bounds
     * that inner list, not the outer string_agg() one. A '(', ')' or ','
     * inside a literal/quoted identifier/comment/dollar-quoted body -
     * scanLine()'s claimed spans, same idea as in statementBounds() - does
     * not count, and neither does one nested a level deeper: `left(...)`'s
     * own comma above is not reported for the outer list.
     *
     * With \a pos < 0, the *first* top-level (outermost, earliest opened)
     * list in the whole text is reported - the parameter list right after
     * `CREATE [OR REPLACE] FUNCTION|PROCEDURE name`, for every caller that
     * uses this so far, since nothing legitimately opens a bracket ahead of
     * it in such a script (a leading comment, e.g. the commented-out
     * `DROP FUNCTION ...` some content scripts prepend, is skipped like any
     * other claimed span).
     *
     * \a open is left at -1 (with \a close and \a separators empty) when
     * nothing matches at all: with \a pos >= 0, it sits outside any
     * bracketed list - a bare, unparenthesized list (a plain `select a, b, c`
     * field list, say) is not something this finds the bounds of, and is not
     * attempted; with \a pos < 0, the text has no top-level bracket to speak
     * of.
     */
    SqlListBounds listBounds(const QString &text, int pos) const;

    /*!
     * \brief Bounds of the list within an explicit range - what "Split list
     *        into lines" uses instead of searching for an enclosing bracket
     *        when the person has something selected: the selection itself
     *        says what "the list" is, so there is nothing to search for.
     * \param text  the whole script (scanned from its own start regardless of
     *              \a from - the lexer's own state, such as which
     *              quoting/comment mode it is in, only makes sense read
     *              continuously from the top, same as in listBounds())
     * \param from, to  the range, e.g. a selection's bounds (\a to exclusive)
     * \return every top-level ',' in \c [from, to) as a separator -
     *         "top-level" meaning "not inside a bracket that itself opened
     *         inside the range", so a comma in a function call fully inside
     *         the selection is skipped same as elsewhere, but nothing is
     *         asked about what encloses the range from the outside: a
     *         selection of `a, b, c` out of `foo(a, b, c, d)` is its own
     *         three-item list, whether or not the enclosing foo(...) itself
     *         is fully selected. \c SqlListBounds::close == -1 if \a from/
     *         \a to do not describe a non-empty range within \a text.
     */
    SqlListBounds listBoundsInRange(const QString &text, int from, int to) const;

    /*!
     * \brief What it takes to give the list \a bounds points at (see
     *        listBounds()/listBoundsInRange()) one item per line.
     * \param text  the same text \a bounds was computed against
     * \param bounds  a list found by listBounds()/listBoundsInRange();
     *                returns a default-constructed (\c start/\c end == -1)
     *                result if \a bounds.close is -1 - callers are expected
     *                to check that first rather than splice this in
     *                regardless
     * \param unit  one indentation step (a tab, or a run of spaces - see
     *              indentUnit() in settings.h)
     * \return where to splice, and with what - see SqlListReflow
     *
     * Each item is trimmed of its own surrounding whitespace first, so
     * running this again on an already multi-line list (or one formatted by
     * hand some other way) normalizes it rather than piling up blank lines.
     * A comment sitting between two items, though, is swallowed into
     * whichever item's trim reaches it - nothing clever is attempted about
     * where such a comment "belongs".
     *
     * A line break (and the adjacent horizontal whitespace it makes
     * redundant - see horizontalSpaceRun()) is only inserted before the
     * first item / after the last one where one is not already there. On the
     * trailing side that includes a comma: `,c` picking up immediately where
     * a selection of `a, b` out of `a, b, c` left off is the rest of the very
     * same list continuing just past the selection, not foreign content to
     * wall off behind a break of its own - the same reasoning turns a
     * trailing comma *inside* the selection (`a, b,` out of `a, b, c`) into
     * an empty last item once trimmed, which is dropped along with the
     * separator that produced it rather than becoming a blank line, folding
     * back into this same "the list carries on right here" handling. The
     * leading side does not extend the same courtesy to a comma: `b, c`
     * selected out of `a, b, c` still pushes "b" onto a fresh line rather
     * than leaving it glued after "a," - unlike a continuation past the
     * selection, content before it is left completely untouched regardless
     * of what character happens to end it, comma included, the same as
     * `select ` would be.
     *
     * Between items a break is always inserted - that is the entire point -
     * but whether it is followed by \a unit's worth of indentation depends on
     * the very same "did the first item get pushed onto a fresh line"
     * answer: if it did not (nothing above needed touching, so there is no
     * *new* indentation level to align the rest of the list to), none of the
     * other items are indented past that either, rather than only some of
     * them ending up indented relative to a first item that stayed exactly
     * where it was. A `create function name(...)` or a precisely-selected
     * `a, b, c` both get the first kind of treatment; a whole query selected
     * at once, dragging `select` and `from t` along into the first/last
     * item, gets the second - see the class-level notes on why nothing is
     * attempted to tell such a selection apart from a "clean" one; a wider
     * selection is answered less, not guessed at more.
     */
    static SqlListReflow reflowList(const QString &text, const SqlListBounds &bounds, const QString &unit);

    /*!
     * \brief How far a run of plain spaces/tabs starting at \a pos extends -
     *        not newlines, so a blank line right after \a pos stays a blank
     *        line rather than being pulled up into the previous one.
     * \return \a pos plus the run's length; \a pos itself if \a text.at(pos)
     *         is not a space or a tab to begin with
     *
     * What reflowList() uses, on both ends of a list, to tell "genuinely
     * more code follows" apart from "just some whitespace that is about to
     * become redundant" - the space before `from` in `... c from t`, once
     * `c` is pulled onto its own line, say. Exposed on its own since it is a
     * generic enough little scan to be worth reusing rather than inlining.
     */
    static int horizontalSpaceRun(const QString &text, int pos);

    /// lexer built with the connection's hl.conf (nullptr if unavailable)
    static std::shared_ptr<const SqlLexer> sharedFor(DbConnection *con);
    /// to be called on scripts cache invalidation
    static void clearCache();

private:
    enum class LastWordOption { Yes, No, MayBe };
    struct WordInfo
    {
        int group;
        LastWordOption isLastWord;
        QHash<QString, WordInfo> nextWords;
    };

    /// index just past the closing '$' of the $tag$ at `from`, -1 if not a tag
    static int dollarTagEnd(const QString &text, int from);

    /*!
     * \brief Every character of \a text that is actual code - not inside a
     *        literal, a quoted identifier, a comment or a dollar-quoted body
     *        (scanLine()'s claimed spans) - visited once, in order.
     * \param visit  called with each character's absolute position and value;
     *               returning \c false stops the walk right there (used for
     *               an early exit once a caller has found what it needed, the
     *               same way listBounds() used to stop scanning on its own)
     *
     * The line-by-line bookkeeping (state, $-tag, claimed spans per line)
     * that both listBounds() and listBoundsInRange() need to tell code from
     * "not code" is identical between the two - only what they do once a
     * bracket or a comma turns out to be actual code differs - so it lives
     * here once rather than twice.
     */
    void forEachCodeChar(const QString &text, const std::function<bool(int, QChar)> &visit) const;

    QHash<QString, WordInfo> _keywords;
    QSet<QString> _functions;
    QString _delimiters;
    /// hl.conf's `statement_split`: may the script be cut on top-level ';'
    bool _statementSplit = false;
    bool _tsqlBrackets = false;
    int _keywordGroupCount = 0;

};

#endif // SQLLEXER_H
