#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "sqlsyntaxhighlighter.h"

SqlSyntaxHighlighter::SqlSyntaxHighlighter(const QJsonObject &settings, QObject *parent) :
    QSyntaxHighlighter(parent)
{
    /*
     * QTextCharFormat::setFontCapitalization does not work
     * https://bugreports.qt.io/browse/QTBUG-32619
     */

    delimiters = " \t\r\n``'\";:()[]<>{}/\\^&$|!?~,.-+*%=" + settings["add_separators"].toString();

    auto get_format = [](
            const QJsonValue node,
            const QVariant &prop,
            const QColor &defForeground,
            bool bold = false,
            bool italic = false) ->QTextCharFormat
    {
        QTextCharFormat format;
        format.setProperty(QTextFormat::UserProperty, prop);
        format.setForeground(defForeground);
        format.setFontItalic(italic);
        format.setFontWeight(bold ? QFont::Bold : QFont::Normal);
        if (!node.isUndefined() && node.isObject())
        {
            QColor c(node.toObject()["foreground"].toString());
            if (c.isValid())
                format.setForeground(c);
            format.setFontItalic(node.toObject()["italic"].toBool(italic));
            format.setFontWeight(node.toObject()["bold"].toBool(bold) ?
                        QFont::Bold : QFont::Normal);
        }
        return format;
    };

    QTextCharFormat format;
    formats.append(get_format(settings["literal"], "envelope", Qt::red));  // 0 - literal

    format = get_format(settings["identifier"], "envelope", Qt::black);
    tsqlBrackets = settings["identifier"].toObject()["brackets"].toBool(false);
    formats.append(format);  // 1 - ""
    formats.append(format);  // 2 - []

    format = get_format(settings["comment"], "envelope", Qt::darkGreen, false, true);
    formats.append(format);  // 3 - comment /**/
    formats.append(format);  // 4 - comment --

    formats.append(get_format(settings["number"], "code", Qt::darkMagenta, true, false));  // 5 - number
    formats.append(get_format(settings["variable"], "code", {"#4f2b2a"}, false, false));   // 6 - variable

    formats.append(get_format(settings["function"], "code", Qt::darkBlue, true, false));   // 7 - function
    QJsonArray fnDict = settings["function"].toObject()["dict"].toArray();
    for (const QJsonValue &v: fnDict)
    {
        QString kw = v.toString();
        if (!kw.isEmpty())
            functions.insert(kw, 7);
    }

    // keywords, operator-like functions, data types and so on
    QJsonArray kwPartition = settings["keyword"].toArray();
    for (const QJsonValue &p: kwPartition)
    {
        QJsonArray kwDict = p.toObject()["dict"].toArray();
        char ind = static_cast<char>(formats.length());
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
            QHash <QString, WordInfo> *curLevel = &keywords;
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
                        it.value().formatIndex = ind;
                    continue;
                }

                if (i < words.length() - 1)
                    it = curLevel->insert(w, { -1, LastWordOption::No, {}});
                else
                    it = curLevel->insert(w, { ind, LastWordOption::Yes, {}});

                //lwo = &it.value().isLastWord;
                curLevel = &it.value().nextWords;
            }
        }
        formats.append(get_format(p, "code", Qt::black));
    }
}

bool SqlSyntaxHighlighter::isKeyword(const QString &word)
{
    return keywords.contains(word.toLower());
}

void SqlSyntaxHighlighter::highlightBlock(const QString &text)
{
    int firstWordStartPos = -1;
    const WordInfo *lastWordInfo = nullptr;
    QString::ConstIterator i = text.constBegin();
    QChar prevChar;
    int mode = (previousBlockState() == -1 ? 0xFF : previousBlockState());
    int len = 1, pos = 1;

    auto markAscii = [&i, &mode]()
    {
        // set flags to detect encodings mix within single word
        const ushort &uc = (*i).unicode();
        if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z'))
            mode |= 0x00010000;
        else if (uc > 127)
            mode |= 0x00020000;
    };

    do
    {
        switch (mode & 0xFF)
        {
        case 0xFF:
            len = 1;
            if (*i == '\'')
                mode = 0;
            else if (*i == '"')
                mode = 1;
            else if (*i == '[' && tsqlBrackets)
                mode = 2;
            else if (*i == '*' && prevChar == '/')
            {
                // hiword is a nesting level
                mode = 0x00010003;
                ++len;
            }
            else if (*i == '-' && prevChar == '-')
            {
                mode = 4;
                ++len;
            }
            else if (delimiters.contains(prevChar) || prevChar.isNull())
            {
                if ((*i).isDigit())
                    mode = 5;
                else if (// typical start of word
                         (*i).isLetter() || *i == '_' ||
                         // tsql-like vars, temp tables and so on
                         ((*i == '@' || *i == '$' || *i == '#') && !delimiters.contains(*i))
                        )
                {
                    mode = 9;
                    markAscii();
                }
            }

            if (mode != 0xFF)
                setCurrentBlockState(mode);

            break;
        case 0:
            if (*i == '\'')
            {
                setFormat(pos - len, len, formats.at(mode));
                mode = 0xFF;
            }
            break;
        case 1:
            if (*i == '"')
            {
                // check for data type (e.g. "char") or other quoted SINGLE word
                auto const it = keywords.find(text.mid(pos - len, len).toLower());
                if (it != keywords.end() && it.value().formatIndex >= 0)
                    setFormat(pos - len, len, formats.at(it.value().formatIndex));
                else
                    setFormat(pos - len, len, formats.at(mode));
                mode = 0xFF;
            }
            break;
        case 2:
            if (*i == ']')
            {
                setFormat(pos - len, len, formats.at(mode));
                mode = 0xFF;
            }
            break;
        case 3:
            /* multiline comments may be nested */
            if ((*i == '*' && prevChar == '/'))
                mode += 0x00010000;
            else if ((*i == '/' && prevChar == '*'))
                mode -= 0x00010000;

            if ((static_cast<unsigned int>(mode) & 0xFFFFFF00) == 0)
            {
                setFormat(pos - len, len, formats.at(mode));
                mode = 0xFF;
            }
            else
                setCurrentBlockState(mode);
            break;
        case 4:
            if (*i == '\n' || (*i).isNull())
            {
                setFormat(pos - len, len, formats.at(mode));
                mode = 0xFF;
                lastWordInfo = nullptr;
            }
            break;
        case 5:
            if (!(*i).isDigit() && *i != '.')
            {
                if (delimiters.contains(*i) || (*i).isNull())
                {
                    setFormat(pos - len, len - 1, formats.at(mode));
                    --i; --pos;
                }
                mode = 0xFF;
            }
            break;
        case 9:
        {
            int delimPos = delimiters.indexOf(*i);
            if (delimPos >= 0 || (*i).isNull())
            {
                QString word = text.mid(pos - len, len - 1).toLower();
                int delta = 0;

                // skip space characters to detect possible trailing '('
                while (delimPos >= 0 && delimPos < 4)
                    delimPos = delimiters.indexOf(*(i + ++delta));

                if (*(i + delta) == '(' && functions.constFind(word) != functions.constEnd())
                    // function
                    setFormat(pos - len, len - 1, formats.at(7));
                else
                {
                    // ms sql variable
                    if (word.at(0) == '@' && len > 2 && word.at(1) != '@')
                        setFormat(pos - len, len - 1, formats.at(6));

                    auto processFirstWord = [&](bool standalone = false) {
                        auto const it = keywords.find(word);
                        if (it != keywords.end())
                        {
                            if (it.value().isLastWord != LastWordOption::No)
                                setFormat(pos - len, len - 1, formats.at(it.value().formatIndex));

                            if (!standalone && it.value().isLastWord != LastWordOption::Yes)
                            {
                                firstWordStartPos = pos - len;
                                lastWordInfo = &(it.value());
                            }
                        }
                        // ascii and non-ascii character within single word
                        else if ((mode >> 16) == 3)
                        {
                            QTextCharFormat format;
                            format.setUnderlineColor(Qt::red);
                            format.setUnderlineStyle(QTextCharFormat::DotLine);
                            setFormat(pos - len, len - 1, format);
                        }
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
                                setFormat(firstWordStartPos, pos - firstWordStartPos - 1, formats.at(it.value().formatIndex));
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

                --i; --pos;
                mode = 0xFF;
            }
            else
                markAscii();

            break;
        }
        default:
            setFormat(pos - len, len - 1, formats.at(mode & 0xFF));
            break;
        }
        prevChar = *i;
        ++len;

        if (i == text.constEnd())
        {
            --len;
            break;
        }
        ++i; ++pos;
    }
    while (true);

    if (mode != 0xFF)
    {
        setFormat(pos - len, len - 1, formats.at(mode & 0xFF));
        if ((mode & 0xFF) > 3)
            mode = 0xFF;
    }

    if (currentBlockState() != mode)
        setCurrentBlockState(mode);

}
