#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "jsonsyntaxhighlighter.h"
#include "settings.h"

JsonSyntaxHighlighter::JsonSyntaxHighlighter(const QJsonDocument &settings, QObject *parent) :
    QSyntaxHighlighter(parent)
{
    formats.append(hlFormat(settings["string"], "envelope", Qt::darkGreen));        // 0 - double quoted text
    formats.append(hlFormat(settings["number"], "code", Qt::darkRed));              // 1 - number
    formats.append(hlFormat(settings["node_name"], "envelope", Qt::darkBlue));      // 2 - node name
    formats.append(hlFormat(settings["bool"], "code", Qt::blue));                   // 3 - true, false
    formats.append(hlFormat(settings["null"], "code", Qt::darkGray, false, true));  // 4 - null
    formats.append(hlFormat(settings["error"], "code", Qt::red));                   // 5 - err literal
 }

void JsonSyntaxHighlighter::highlightBlock(const QString &text)
{
    QString::ConstIterator i = text.constBegin();
    QChar prevChar;
    int mode = (previousBlockState() == -1 ? 0xFF : previousBlockState());
    int len = 1, pos = 1;

    do
    {
        switch (mode & 0xFF)
        {
        case 0xFF:
            len = 1;
            if (*i == '"')
                mode = 0;
            else if (delimiters.contains(prevChar) || prevChar.isNull())
            {
                if ((*i).isDigit() || *i == '-' || *i == '+')
                    mode = 1;
                else if (!delimiters.contains(*i))
                    mode = 9;
            }

            if (mode != 0xFF)
                setCurrentBlockState(mode);

            break;
        case 0:
            if (*i == '\\')
            {
                // skip next character
                ++i;
                ++len;
                ++pos;
            }
            else if (*i == '"')
            {
                // search for trailing ':'
                int delta = 0;
                int delimPos;
                do
                {
                    delimPos = delimiters.indexOf(*(i + ++delta));
                }
                while (delimPos >= 0 && delimPos < 4);

                if (*(i + delta) == ':')
                    setFormat(pos - len, len, formats.at(2));
                else
                    setFormat(pos - len, len, formats.at(0));
                mode = 0xFF;
            }
            break;
        case 1:
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

                if (word == "true" || word == "false")
                    setFormat(pos - len, len - 1, formats.at(3));
                else if (word == "null")
                    setFormat(pos - len, len - 1, formats.at(4));
                else
                    setFormat(pos - len, len - 1, formats.at(5));

                --i; --pos;
                mode = 0xFF;
            }
            break;
        }
        default:
            setFormat(pos - len, len - 1, formats.at(5));
            break;
        }
        prevChar = *i;

        if (i == text.constEnd())
        {
            --len;
            break;
        }

        ++len;
        ++i;
        ++pos;
    }
    while (true);

    if (mode != 0xFF)
    {
        setFormat(pos - len, len - 1, formats.at(5));
        mode = 0xFF;
    }

    if (currentBlockState() != mode)
        setCurrentBlockState(mode);
}

