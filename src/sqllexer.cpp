#include "sqllexer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include "dbconnection.h"
#include "misc.h"
#include "scripting.h"

SqlLexer::SqlLexer(const QJsonDocument &settings)
{
    _delimiters = " \t\r\n``'\";:()[]<>{}/\\^&$|!?~,.-+*%=" + settings["add_separators"].toString();
    _statementSplit = settings["statement_split"].toBool(false);
    _tsqlBrackets = settings["identifier"].toObject()["brackets"].toBool(false);

    const QJsonArray fnDict = settings["function"].toObject()["dict"].toArray();
    for (const QJsonValue &v: fnDict)
    {
        QString kw = v.toString();
        if (!kw.isEmpty())
            _functions.insert(kw);
    }

    // keywords, operator-like functions, data types and so on
    const QJsonArray kwPartition = settings["keyword"].toArray();
    for (const QJsonValue &p: kwPartition)
    {
        const QJsonArray kwDict = p.toObject().value("dict").toArray();
        int group = _keywordGroupCount++;
        for (const QJsonValue &v: kwDict)
        {
            QString kw = v.toString();
            QStringList words = kw.split(' ',
                             #if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
                                         Qt::SkipEmptyParts
                             #else
                                         QString::SkipEmptyParts
                             #endif
                                         );
            QHash <QString, WordInfo> *curLevel = &_keywords;
            LastWordOption *lwo = nullptr;
            for (int i = 0; i < words.length(); ++i)
            {
                QString w = words.at(i);
                auto it = curLevel->find(w);
                if (it != curLevel->end())
                {
                    lwo = &it.value().isLastWord;
                    curLevel = &it.value().nextWords;
                    if (i < words.length() - 1 && *lwo == LastWordOption::Yes)
                        *lwo = LastWordOption::MayBe;
                    else if (i == words.length() - 1 && *lwo == LastWordOption::No)
                        *lwo = LastWordOption::MayBe;

                    if (i == words.length() - 1)
                        it.value().group = group;
                    continue;
                }

                if (i < words.length() - 1)
                    it = curLevel->insert(w, { -1, LastWordOption::No, {}});
                else
                    it = curLevel->insert(w, { group, LastWordOption::Yes, {}});

                curLevel = &it.value().nextWords;
            }
        }
    }
}

bool SqlLexer::isKeyword(const QString &word) const
{
    return _keywords.contains(word.toLower());
}

int SqlLexer::dollarTagEnd(const QString &text, int from)
{
    // $$ or $tag$, where tag looks like an unquoted identifier ($1 is a
    // positional parameter, not a tag)
    for (int i = from + 1; i < text.length(); ++i)
    {
        const QChar c = text.at(i);
        if (c == '$')
            return i + 1;
        bool ok = (c.isLetter() || c == '_' || c.unicode() > 127 ||
                   (c.isDigit() && i > from + 1));
        if (!ok)
            break;
    }
    return -1;
}

int SqlLexer::scanLine(const QString &text,
                       int state,
                       const std::function<void(const Span &)> &report,
                       QString *dollarTag) const
{
    const int length = text.length();
    // the scanner peeks the trailing '\0' to complete the tokens ending the line
    auto charAt = [&text, length](int index) -> QChar {
        return (index >= 0 && index < length ? text.at(index) : QChar());
    };
    // 'emit' is a Qt macro, hence the name
    auto addSpan = [&report, length](int from, int len, Token token, int group = -1) {
        if (from < 0)
        {
            len += from;
            from = 0;
        }
        if (len > length - from)
            len = length - from;
        if (len > 0)
            report({from, len, token, group});
    };

    int mode = (state == -1 ? InitialState : state);
    int start = 0;

    // finish the dollar quoted body started on one of the previous lines
    if (dollarTag && !dollarTag->isEmpty())
    {
        int close = text.indexOf(*dollarTag);
        if (close < 0)
        {
            addSpan(0, length, Token::DollarQuoted);
            return mode;
        }
        start = close + dollarTag->length();
        addSpan(0, start, Token::DollarQuoted);
        dollarTag->clear();
    }

    int firstWordStartPos = -1;
    const WordInfo *lastWordInfo = nullptr;
    QChar prevChar = charAt(start - 1);
    int tokenStart = start;
    int p = start;

    auto markAscii = [&mode](QChar c)
    {
        // set flags to detect encodings mix within single word
        const ushort uc = c.unicode();
        if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z'))
            mode |= 0x00010000;
        else if (uc > 127)
            mode |= 0x00020000;
    };

    for (;; ++p)
    {
        const QChar c = charAt(p);
        switch (mode & 0xFF)
        {
        case 0xFF:
            tokenStart = p;
            if (c == '\'')
                mode = 0;
            else if (c == '"')
                mode = 1;
            else if (c == '[' && _tsqlBrackets)
                mode = 2;
            else if (c == '*' && prevChar == '/')
            {
                // hiword is a nesting level
                mode = 0x00010003;
                --tokenStart;
            }
            else if (c == '-' && prevChar == '-')
            {
                mode = 4;
                --tokenStart;
            }
            else if (dollarTag && c == '$')
            {
                int tagEnd = dollarTagEnd(text, p);
                if (tagEnd > 0)
                {
                    const QString tag = text.mid(p, tagEnd - p);
                    int close = text.indexOf(tag, tagEnd);
                    if (close < 0)
                    {
                        // to be continued on the next line
                        addSpan(p, length - p, Token::DollarQuoted);
                        *dollarTag = tag;
                        return InitialState;
                    }
                    int bodyEnd = close + tag.length();
                    addSpan(p, bodyEnd - p, Token::DollarQuoted);
                    prevChar = '$';
                    p = bodyEnd - 1;
                    continue;
                }
            }

            if ((mode & 0xFF) == 0xFF &&
                (_delimiters.contains(prevChar) || prevChar.isNull()))
            {
                if (c.isDigit())
                    mode = 5;
                else if (// typical start of word
                         c.isLetter() || c == '_' ||
                         // tsql-like vars, temp tables and so on
                         ((c == '@' || c == '$' || c == '#') && !_delimiters.contains(c))
                        )
                {
                    mode = 9;
                    markAscii(c);
                }
            }
            break;
        case 0:
            if (c == '\'')
            {
                addSpan(tokenStart, p - tokenStart + 1, Token::Literal);
                mode = InitialState;
            }
            break;
        case 1:
            if (c == '"')
            {
                // check for data type (e.g. "char") or other quoted SINGLE word
                const int len = p - tokenStart + 1;
                auto const it = _keywords.find(text.mid(tokenStart, len).toLower());
                if (it != _keywords.end() && it.value().group >= 0)
                    addSpan(tokenStart, len, Token::Keyword, it.value().group);
                else
                    addSpan(tokenStart, len, Token::DoubleQuoted);
                mode = InitialState;
            }
            break;
        case 2:
            if (c == ']')
            {
                addSpan(tokenStart, p - tokenStart + 1, Token::BracketQuoted);
                mode = InitialState;
            }
            break;
        case 3:
            /* multiline comments may be nested */
            if (c == '*' && prevChar == '/')
                mode += 0x00010000;
            else if (c == '/' && prevChar == '*')
                mode -= 0x00010000;

            if ((static_cast<unsigned int>(mode) & 0xFFFFFF00) == 0)
            {
                addSpan(tokenStart, p - tokenStart + 1, Token::CommentBlock);
                mode = InitialState;
            }
            break;
        case 4:
            if (c.isNull())
            {
                addSpan(tokenStart, p - tokenStart + 1, Token::CommentLine);
                mode = InitialState;
                lastWordInfo = nullptr;
            }
            break;
        case 5:
            if (!c.isDigit() && c != '.')
            {
                if (_delimiters.contains(c) || c.isNull())
                {
                    addSpan(tokenStart, p - tokenStart, Token::Number);
                    --p;
                }
                mode = InitialState;
            }
            break;
        case 9:
        {
            int delimPos = _delimiters.indexOf(c);
            if (delimPos >= 0 || c.isNull())
            {
                const int len = p - tokenStart;
                QString word = text.mid(tokenStart, len).toLower();
                int delta = 0;

                // skip space characters to detect possible trailing '('
                while (delimPos >= 0 && delimPos < 4)
                    delimPos = _delimiters.indexOf(charAt(p + ++delta));

                if (charAt(p + delta) == '(' && _functions.contains(word))
                    // function
                    addSpan(tokenStart, len, Token::Function);
                else
                {
                    // ms sql variable
                    if (word.at(0) == '@' && len > 1 && word.at(1) != '@')
                        addSpan(tokenStart, len, Token::Variable);

                    auto processFirstWord = [&](bool standalone = false) {
                        auto const it = _keywords.find(word);
                        if (it != _keywords.end())
                        {
                            if (it.value().isLastWord != LastWordOption::No)
                                addSpan(tokenStart, len, Token::Keyword, it.value().group);

                            if (!standalone && it.value().isLastWord != LastWordOption::Yes)
                            {
                                firstWordStartPos = tokenStart;
                                lastWordInfo = &(it.value());
                            }
                        }
                        // ascii and non-ascii character within single word
                        else if ((mode >> 16) == 3)
                            addSpan(tokenStart, len, Token::MixedEncoding);
                    };

                    // precess data types (may be multi-word)
                    if (!lastWordInfo)
                    {
                        processFirstWord();
                    }
                    else
                    {
                        auto const it = lastWordInfo->nextWords.find(word);
                        if (it != lastWordInfo->nextWords.end())
                        {
                            if (it.value().isLastWord != LastWordOption::No)
                                addSpan(firstWordStartPos, p - firstWordStartPos,
                                        Token::Keyword, it.value().group);
                            else
                            {
                                lastWordInfo = &(it.value());
                                // apply "default" color untill end of phrase get found
                                processFirstWord(true);
                            }
                        }
                        else
                        {
                            // incomplete phrase - restart search
                            lastWordInfo = nullptr;
                            processFirstWord();
                        }
                    }
                }

                if (delimPos > 2)
                    lastWordInfo = nullptr;

                --p;
                mode = InitialState;
            }
            else
                markAscii(c);

            break;
        }
        default:
            break;
        }

        prevChar = charAt(p);
        if (p >= length)
            break;
    }

    if (mode != InitialState)
    {
        static const Token tailTokens[] = { Token::Literal, Token::DoubleQuoted,
                                            Token::BracketQuoted, Token::CommentBlock,
                                            Token::CommentLine };
        const int tail = mode & 0xFF;
        if (tail < int(sizeof(tailTokens) / sizeof(tailTokens[0])))
            addSpan(tokenStart, length - tokenStart, tailTokens[tail]);
        if (tail > 3)
            mode = InitialState;
    }

    return mode;
}

QPair<int, int> SqlLexer::statementBounds(const QString &text, int pos) const
{
    // ';' is not a statement boundary in this dialect - see canSplitStatements()
    if (!_statementSplit)
        return {-1, -1};

    QString dollarTag;
    int state = InitialState;
    int lineStart = 0;
    int stmtStart = 0;

    auto trimmed = [&text](int from, int to) -> QPair<int, int>
    {
        while (from < to && text.at(from).isSpace())
            ++from;
        while (to > from && text.at(to - 1).isSpace())
            --to;
        return {from, to};
    };

    while (true)
    {
        const int nl = text.indexOf('\n', lineStart);
        const int lineEnd = (nl < 0 ? text.length() : nl);
        const QString line = text.mid(lineStart, lineEnd - lineStart);

        // spans scanLine() claims on this line: a separator inside any of them
        // is part of a literal/comment/quoted body, not a statement separator
        QVector<QPair<int, int>> claimed;
        state = scanLine(line, state, [&claimed](const Span &s)
        {
            claimed.append({s.start, s.start + s.length});
        }, &dollarTag);

        for (int i = line.indexOf(';'); i >= 0; i = line.indexOf(';', i + 1))
        {
            bool isClaimed = false;
            for (const auto &s: claimed)
            {
                if (i >= s.first && i < s.second)
                {
                    isClaimed = true;
                    break;
                }
            }
            if (isClaimed)
                continue;

            // The caret right before the ';' still belongs to this statement,
            // right after it to the next one.
            const int abs = lineStart + i;
            if (abs >= pos)
                return trimmed(stmtStart, abs + 1);
            stmtStart = abs + 1;
        }

        if (nl < 0)
            break;
        lineStart = nl + 1;
    }

    return trimmed(stmtStart, text.length());
}

SqlListBounds SqlLexer::listBounds(const QString &text, int pos) const
{
    SqlListBounds result;

    QString dollarTag;
    int state = InitialState;
    int lineStart = 0;

    // Bracket nesting, tracked the same way statementBounds() tracks ';': a
    // '(', ')' or ',' inside a span scanLine() claims (a literal, a quoted
    // identifier, a comment, a dollar-quoted body) is not code and does not
    // count. Each open bracket carries the top-level commas seen since it was
    // pushed - "top-level" meaning "at this bracket's own nesting", since a
    // deeper one pushes (and pops) its own entry.
    struct Level { int open; QVector<int> commas; };
    QVector<Level> stack;
    // No caret to aim at: the first pair to close back to zero nesting is, by
    // construction, the first one that ever opened (nothing else at depth 0
    // can open before it closes) - which is exactly the routine's own
    // argument list for every caller that asks for this.
    const bool wantFirstTopLevel = (pos < 0);

    while (true)
    {
        const int nl = text.indexOf('\n', lineStart);
        const int lineEnd = (nl < 0 ? text.length() : nl);
        const QString line = text.mid(lineStart, lineEnd - lineStart);

        QVector<QPair<int, int>> claimed;
        state = scanLine(line, state, [&claimed](const Span &s)
        {
            claimed.append({s.start, s.start + s.length});
        }, &dollarTag);

        for (int i = 0; i < line.length(); ++i)
        {
            bool isClaimed = false;
            for (const auto &s: claimed)
            {
                if (i >= s.first && i < s.second)
                {
                    isClaimed = true;
                    break;
                }
            }
            if (isClaimed)
                continue;

            const QChar ch = line.at(i);
            const int abs = lineStart + i;
            if (ch == QLatin1Char('('))
            {
                stack.append(Level{abs, {}});
            }
            else if (ch == QLatin1Char(')'))
            {
                if (stack.isEmpty())
                    continue;
                const Level lvl = stack.takeLast();
                // With a caret to satisfy, the first bracket whose range
                // contains it - scanned left to right, closing brackets in
                // document order - is necessarily the innermost one: any pair
                // properly containing the caret and closing earlier in the
                // text would have to be nested inside this one, not around it.
                const bool isMatch = wantFirstTopLevel ? stack.isEmpty()
                                                        : (pos > lvl.open && pos <= abs);
                if (isMatch)
                {
                    result.open = lvl.open;
                    result.close = abs;
                    result.separators = lvl.commas;
                    return result;
                }
            }
            else if (ch == QLatin1Char(',') && !stack.isEmpty())
            {
                stack.last().commas.append(abs);
            }
        }

        if (nl < 0)
            break;
        lineStart = nl + 1;
    }

    return result;
}

QString SqlLexer::reflowList(const QString &text, const SqlListBounds &bounds, const QString &unit)
{
    if (bounds.open < 0)
        return QString(); // nothing found - callers are expected not to splice this in at all

    // The indentation already sitting on the line the '(' is on - the items
    // go one further than that, the closing ')' stays right at it, so the
    // result lines up with whatever the list belongs to (a `create function`
    // header, a call, ...) rather than with the caret.
    const int lineStart = text.lastIndexOf('\n', bounds.open) + 1;
    int textStart = lineStart;
    while (textStart < bounds.open && text.at(textStart).isSpace())
        ++textStart;
    const QString baseIndent = text.mid(lineStart, textStart - lineStart);
    const QString itemIndent = baseIndent + unit;

    // Item boundaries: open+1..separators[0], between separators, and
    // separators.last()+1..close - each trimmed of its own whitespace so a
    // list already spread over several lines (a previous run of this very
    // command, or hand formatting) is normalized rather than accumulating
    // blank lines or drifting indentation.
    QVector<int> cuts = bounds.separators;
    cuts.prepend(bounds.open);
    cuts.append(bounds.close);

    QStringList items;
    items.reserve(cuts.size() - 1);
    for (int i = 0; i + 1 < cuts.size(); ++i)
    {
        const int from = cuts.at(i) + 1;
        const int to = cuts.at(i + 1);
        items.append(text.mid(from, to - from).trimmed());
    }

    return "\n" + itemIndent + items.join(",\n" + itemIndent) + "\n" + baseIndent;
}

QString SqlLexer::foldKeywords(const QString &script) const
{
    QString res = script;
    QString dollarTag;
    int state = InitialState;
    int lineStart = 0;

    while (true)
    {
        const int nl = script.indexOf('\n', lineStart);
        const int lineEnd = (nl < 0 ? script.length() : nl);
        const QString line = script.mid(lineStart, lineEnd - lineStart);

        state = scanLine(line, state, [&](const Span &s)
        {
            if (s.token != Token::Keyword && s.token != Token::Function)
                return;
            // a quoted identifier may be reported as a keyword (e.g. "char"),
            // but its case is significant
            if (line.at(s.start) == '"')
                return;

            // per character folding keeps the text length intact
            for (int i = s.start; i < s.start + s.length; ++i)
                res[lineStart + i] = line.at(i).toLower();
        }, &dollarTag);

        if (nl < 0)
            break;
        lineStart = nl + 1;
    }
    return res;
}

// key = dbms_scripting_id
static QHash<QString, std::shared_ptr<const SqlLexer>> lexerCache;

std::shared_ptr<const SqlLexer> SqlLexer::sharedFor(DbConnection *con)
{
    if (!con)
        return nullptr;

    const auto it = lexerCache.find(con->dbmsScriptingID());
    if (it != lexerCache.end())
        return it.value();

    QJsonDocument settings;
    try
    {
        // Through the locator, not by the relative path alone: dbmsScriptPath()
        // returns a path relative to a *resource root*, so handing it straight
        // to QFile resolved it against the process' working directory - which is
        // a root only when the application happens to be started from its own
        // folder. Everywhere else the file was simply not found and the catch
        // below silently turned that into "no dictionaries", disabling the
        // statement split and the keyword folding.
        const QString file = Scripting::dbmsFile(con, "hl.conf");
        if (file.isEmpty())
            return nullptr;
        settings = readJsonFile(file);
    }
    catch (const QString &)
    {
        // no highlighting settings - no dictionaries to rely on
        return nullptr;
    }

    auto lexer = std::make_shared<const SqlLexer>(settings);
    lexerCache.insert(con->dbmsScriptingID(), lexer);
    return lexer;
}

void SqlLexer::clearCache()
{
    lexerCache.clear();
}
