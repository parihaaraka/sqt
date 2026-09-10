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

/// A parenthesized, comma-separated list: a function/procedure argument list,
/// an IN (...) or VALUES (...) list, a column list and so on.
/// \see SqlLexer::listBounds()
struct SqlListBounds
{
    int open = -1;   ///< position of the opening '(', -1 if nothing was found
    int close = -1;  ///< position of the closing ')'
    QVector<int> separators; ///< positions of the top-level ',' in between
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
     * \brief The text that belongs between \a bounds.open and \a bounds.close
     *        (see listBounds()) to give the list one item per line.
     * \param text  the same text \a bounds was computed against
     * \param bounds  a list found by listBounds(); returns an empty string if
     *                \a bounds.open is -1 - callers are expected to check
     *                that first rather than splice this in regardless
     * \param unit  one indentation step (a tab, or a run of spaces - see
     *              indentUnit() in misc.h)
     * \return replacement for the `[bounds.open+1, bounds.close)` slice of
     *         \a text: each item on its own line, indented one \a unit past
     *         whatever the line \a bounds.open sits on is indented with,
     *         and the closing bracket's own line back at that same
     *         indentation - lining the whole thing up with the call or
     *         declaration around it rather than with wherever the caret (or
     *         the search) happened to land.
     *
     * Each item is trimmed of its own surrounding whitespace first, so
     * running this again on an already multi-line list (or one formatted by
     * hand some other way) normalizes it rather than piling up blank lines.
     * A comment sitting between two items, though, is swallowed into
     * whichever item's trim reaches it - nothing clever is attempted about
     * where such a comment "belongs".
     */
    static QString reflowList(const QString &text, const SqlListBounds &bounds, const QString &unit);

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

    QHash<QString, WordInfo> _keywords;
    QSet<QString> _functions;
    QString _delimiters;
    /// hl.conf's `statement_split`: may the script be cut on top-level ';'
    bool _statementSplit = false;
    bool _tsqlBrackets = false;
    int _keywordGroupCount = 0;

};

#endif // SQLLEXER_H
