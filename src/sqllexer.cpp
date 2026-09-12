#include "sqllexer.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

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

void SqlLexer::forEachCodeChar(const QString &text, const std::function<bool(int, QChar)> &visit) const
{
    QString dollarTag;
    int state = InitialState;
    int lineStart = 0;

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

            if (!visit(lineStart + i, line.at(i)))
                return;
        }

        if (nl < 0)
            break;
        lineStart = nl + 1;
    }
}

SqlListBounds SqlLexer::listBounds(const QString &text, int pos) const
{
    SqlListBounds result;

    // Bracket nesting, tracked the same way statementBounds() tracks ';': a
    // '(', ')' or ',' that is not actual code (forEachCodeChar() already
    // filtered those out) does not count. Each open bracket carries the
    // top-level commas seen since it was pushed - "top-level" meaning "at
    // this bracket's own nesting", since a deeper one pushes (and pops) its
    // own entry.
    struct Level { int open; QVector<int> commas; };
    QVector<Level> stack;
    // No caret to aim at: the first pair to close back to zero nesting is, by
    // construction, the first one that ever opened (nothing else at depth 0
    // can open before it closes) - which is exactly the routine's own
    // argument list for every caller that asks for this.
    const bool wantFirstTopLevel = (pos < 0);

    forEachCodeChar(text, [&](int abs, QChar ch)
    {
        if (ch == QLatin1Char('('))
        {
            stack.append(Level{abs, {}});
        }
        else if (ch == QLatin1Char(')'))
        {
            if (stack.isEmpty())
                return true;
            const Level lvl = stack.takeLast();
            // With a caret to satisfy, the first bracket whose range contains
            // it - scanned left to right, closing brackets in document order
            // - is necessarily the innermost one: any pair properly
            // containing the caret and closing earlier in the text would
            // have to be nested inside this one, not around it.
            const bool isMatch = wantFirstTopLevel ? stack.isEmpty()
                                                    : (pos > lvl.open && pos <= abs);
            if (isMatch)
            {
                result.open = lvl.open;
                result.close = abs;
                result.separators = lvl.commas;
                return false; // found it, nothing past it is of interest
            }
        }
        else if (ch == QLatin1Char(',') && !stack.isEmpty())
        {
            stack.last().commas.append(abs);
        }
        return true;
    });

    return result;
}

SqlListBounds SqlLexer::listBoundsInRange(const QString &text, int from, int to) const
{
    SqlListBounds result;
    if (from < 0 || to < from || to > text.length())
        return result; // close stays -1: the sentinel, see the struct's docs

    // Depth relative to `from`, not to the document: a bracket opened before
    // the range is none of the range's business (see the method's own docs -
    // `a, b, c` selected out of `foo(a, b, c, d)` is its own three-item list
    // regardless of foo(...) around it), so counting starts fresh at 0 right
    // there. forEachCodeChar() still walks from the very top of `text`
    // regardless - the lexer's *own* state (which quoting/comment mode it is
    // in) only makes sense read continuously from there, same as listBounds().
    int depth = 0;

    forEachCodeChar(text, [&](int abs, QChar ch)
    {
        if (abs < from)
            return true;
        if (abs >= to)
            return false; // past the range, nothing left to look at

        if (ch == QLatin1Char('('))
            ++depth;
        else if (ch == QLatin1Char(')'))
        {
            if (depth > 0)
                --depth;
        }
        else if (ch == QLatin1Char(',') && depth == 0)
            result.separators.append(abs);
        return true;
    });

    result.open = from - 1;
    result.close = to;
    return result;
}

SqlListReflow SqlLexer::reflowList(const QString &text, const SqlListBounds &bounds, const QString &unit)
{
    SqlListReflow result;
    if (bounds.close < 0)
        return result; // nothing found - start/end stay -1, see the struct's docs

    // The selection's effective end: trailing whitespace of any kind right
    // before bounds.close - including a line break - is not part of the
    // list's own content. Whether a selection happened to sweep up the
    // newline at the end of its last line, or stopped one character short of
    // it, should not change the result, so that is normalized away up front
    // rather than left to survive as part of the last item's raw text and
    // then be silently swallowed by that item's own trimmed() below, taking
    // the line break it represented with it - which is what used to turn a
    // blank line right after the selection into no blank line at all.
    int effectiveClose = bounds.close;
    while (effectiveClose > 0 && text.at(effectiveClose - 1).isSpace())
        --effectiveClose;

    // Item boundaries: open+1..separators[0], between separators, and
    // separators.last()+1..effectiveClose.
    QVector<int> cuts = bounds.separators;
    cuts.prepend(bounds.open);
    cuts.append(effectiveClose);

    QStringList items;
    items.reserve(cuts.size() - 1);
    for (int i = 0; i + 1 < cuts.size(); ++i)
    {
        const int from = cuts.at(i) + 1;
        const int to = cuts.at(i + 1);
        items.append(text.mid(from, to - from).trimmed());
    }

    // A selection ending (or starting) right on a comma rather than on real
    // content - `a, b,` selected out of `a, b, c` - produces an empty edge
    // item once trimmed, not a blank line worth keeping: it means the very
    // same list carries on right where the selection's edge happens to sit,
    // exactly the situation needsTrailingBreak already knows how to leave
    // alone below once the edge lines up with that comma in the original
    // text instead of with an item's worth of real content. So the empty
    // edge is dropped along with the separator that produced it, folding it
    // into that same handling rather than needing a second concept for it.
    while (items.size() > 1 && items.constFirst().isEmpty())
    {
        items.removeFirst();
        cuts.removeFirst();
    }
    while (items.size() > 1 && items.constLast().isEmpty())
    {
        items.removeLast();
        cuts.removeLast();
    }
    const int effectiveOpen = cuts.constFirst();
    effectiveClose = cuts.constLast();

    // The indentation already sitting on the line effectiveOpen is on - used
    // for the items themselves (see effectiveItemIndent below for when it
    // actually applies) and for the line the closing side gets pushed onto.
    // effectiveOpen == -1 is not "nothing found" here (see SqlListBounds' own
    // docs on bounds.open) but literally "the text before it is empty" - so
    // it is handled directly rather than handed to lastIndexOf(), whose own
    // -1 means "search from the end". The scan itself is not bounded by
    // effectiveOpen the way an earlier version of this bounded it by
    // bounds.open: that bound is exactly what made it find nothing whenever
    // the position was -1 to begin with (any position fails "< -1"), even
    // though the line in question can very well have leading whitespace of
    // its own to report - the loop simply runs until real content (or a line
    // break) the same as it would for any other line.
    const int homeLineStart = effectiveOpen < 0 ? 0 : text.lastIndexOf('\n', effectiveOpen) + 1;
    int homeTextStart = homeLineStart;
    while (homeTextStart < text.length() &&
           (text.at(homeTextStart) == QLatin1Char(' ') || text.at(homeTextStart) == QLatin1Char('\t')))
        ++homeTextStart;
    const QString baseIndent = text.mid(homeLineStart, homeTextStart - homeLineStart);
    const QString itemIndent = baseIndent + unit;

    // Whether a line break belongs before the first item: not if one is
    // already there, once horizontal whitespace (but not a genuine line
    // break) is looked past - `foo(` still has real code right before the
    // list, `a, b` sitting right after "select " does too (that lone space
    // is what the backward scan below absorbs), but a selection starting at
    // the very top of `text`, or right after an existing '\n', does not.
    // Unlike the trailing side below, a comma right before the selection
    // does *not* count as "already fine": `b, c` selected out of `a, b, c`
    // still pushes "b" onto its own fresh line rather than leaving it glued
    // after "a," - "a," is untouched, foreign content as far as this
    // selection is concerned, same as "select " would be, not something to
    // treat as already being in the right shape.
    int leadStart = effectiveOpen + 1;
    while (leadStart > 0 && (text.at(leadStart - 1) == QLatin1Char(' ') || text.at(leadStart - 1) == QLatin1Char('\t')))
        --leadStart;
    const bool leadAlreadyFine = leadStart == 0 || text.at(leadStart - 1) == QLatin1Char('\n');
    const bool needsLeadingBreak = !leadAlreadyFine;

    // Same question, the trailing side; horizontalSpaceRun() does the
    // forward scan directly, since that one is also useful to callers on its
    // own (absorbing what a bracket-search result's close, sitting on ')'
    // itself, never has any of to begin with). A comma *does* count as
    // "already fine" here, unlike above: `,c` picking up immediately where a
    // selection of `a, b` out of `a, b, c` left off is the rest of the very
    // same list continuing just past the selection, not foreign content to
    // wall off behind a break of its own.
    const int trailEnd = effectiveClose + horizontalSpaceRun(text, effectiveClose);
    const bool trailAlreadyFine = trailEnd == text.length() || text.at(trailEnd) == QLatin1Char('\n')
                                   || text.at(trailEnd) == QLatin1Char(',');
    const bool needsTrailingBreak = !trailAlreadyFine;

    // If the first item is not getting pushed onto a fresh line, there is no
    // *new* indentation level to put the rest of the list at either - it
    // stays at whatever the line it is glued to already sits at (baseIndent),
    // the same as a person continuing a statement on the same line rather
    // than opening a nested block for it. Only a genuine fresh line - the
    // first item actually getting pushed onto one - earns the extra step:
    // `create function foo(` or a precisely-selected `a, b, c` both do; a
    // whole line selected `select` and all does not, and `select a, b`
    // becomes `select a,\nb`, not `select a,\n\tb` with the second item
    // oddly indented relative to a first one that stayed exactly where it
    // was.
    const QString effectiveItemIndent = needsLeadingBreak ? itemIndent : baseIndent;

    QString replacement;
    if (needsLeadingBreak)
        replacement += QLatin1Char('\n');
    replacement += effectiveItemIndent + items.join(",\n" + effectiveItemIndent);
    if (needsTrailingBreak)
        replacement += QLatin1Char('\n') + baseIndent;

    result.start = leadStart;
    result.end = trailEnd;
    result.replacement = replacement;
    return result;
}


int SqlLexer::horizontalSpaceRun(const QString &text, int pos)
{
    int end = qMax(pos, 0);
    while (end < text.length() && (text.at(end) == QLatin1Char(' ') || text.at(end) == QLatin1Char('\t')))
        ++end;
    return end - pos;
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

