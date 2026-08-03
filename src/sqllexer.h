#ifndef SQLLEXER_H
#define SQLLEXER_H

#include <QHash>
#include <QSet>
#include <QString>
#include <functional>
#include <memory>

class QJsonDocument;
class DbConnection;

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
    bool _tsqlBrackets = false;
    int _keywordGroupCount = 0;
};

#endif // SQLLEXER_H
