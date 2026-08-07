#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTextDocument>
#include <QTextOption>
#include <qpalette.h>
#include "sqlsyntaxhighlighter.h"
#include "settings.h"

// Alpha value used to make tab/space markers "barely visible"
// (0 = fully transparent, 255 = fully opaque)
static const int kWhitespaceMarkerAlpha = 35;

SqlSyntaxHighlighter::SqlSyntaxHighlighter(const QJsonDocument &settings, QObject *parent) :
    QSyntaxHighlighter(parent), _lexer(std::make_shared<const SqlLexer>(settings))
{
    initFormats(settings);
}

SqlSyntaxHighlighter::SqlSyntaxHighlighter(std::shared_ptr<const SqlLexer> lexer,
                                           const QJsonDocument &settings,
                                           QObject *parent) :
    QSyntaxHighlighter(parent),
    _lexer(lexer ? lexer : std::make_shared<const SqlLexer>(settings))
{
    initFormats(settings);
}

void SqlSyntaxHighlighter::initFormats(const QJsonDocument &settings)
{
    /*
     * QTextCharFormat::setFontCapitalization does not work
     * https://bugreports.qt.io/browse/QTBUG-32619
     */

    QTextCharFormat format;
    formats.resize(int(SqlLexer::Token::MixedEncoding) + 1);

    formats[int(SqlLexer::Token::Literal)] =
            hlFormat(settings["literal"], "envelope", Qt::red);

    format = hlFormat(settings["identifier"], "envelope", Qt::black);
    formats[int(SqlLexer::Token::DoubleQuoted)] = format;
    formats[int(SqlLexer::Token::BracketQuoted)] = format;

    format = hlFormat(settings["comment"], "envelope", Qt::darkGreen, false, true);
    formats[int(SqlLexer::Token::CommentBlock)] = format;
    formats[int(SqlLexer::Token::CommentLine)] = format;

    formats[int(SqlLexer::Token::Number)] =
            hlFormat(settings["number"], "code", Qt::darkMagenta, true, false);
    formats[int(SqlLexer::Token::Variable)] =
            hlFormat(settings["variable"], "code", {"#4f2b2a"}, false, false); // NOLINT
    formats[int(SqlLexer::Token::Function)] =
            hlFormat(settings["function"], "code", Qt::darkBlue, true, false);

    // dollar quoted bodies are highlighted as a usual sql (the scanner reports
    // no such token unless it is asked to), hence the default format here

    mixedEncodingFormat.setUnderlineColor(Qt::red);
    mixedEncodingFormat.setUnderlineStyle(QTextCharFormat::DotLine);
    formats[int(SqlLexer::Token::MixedEncoding)] = mixedEncodingFormat;

    // keywords, operator-like functions, data types and so on
    const QJsonArray kwPartition = settings["keyword"].toArray();
    for (const QJsonValue &p: kwPartition)
        keywordFormats.append(hlFormat(p, "code", Qt::black));
}

bool SqlSyntaxHighlighter::isKeyword(const QString &word)
{
    return _lexer->isKeyword(word);
}

void SqlSyntaxHighlighter::highlightBlock(const QString &text)
{
    const int state = _lexer->scanLine(
                text,
                (previousBlockState() == -1 ? SqlLexer::InitialState : previousBlockState()),
                [this](const SqlLexer::Span &s)
    {
        if (s.token == SqlLexer::Token::Keyword)
        {
            if (s.group >= 0 && s.group < keywordFormats.size())
                setFormat(s.start, s.length, keywordFormats.at(s.group));
        }
        else
            setFormat(s.start, s.length, formats.at(int(s.token)));
    });

    if (currentBlockState() != state)
        setCurrentBlockState(state);

    fadeWhitespaceMarkers(text);
}

void SqlSyntaxHighlighter::fadeWhitespaceMarkers(const QString &text)
{
    if (!document() ||
        !(document()->defaultTextOption().flags() & QTextOption::ShowTabsAndSpaces))
        return;

    int runStart = -1;
    for (int idx = 0; idx <= text.length(); ++idx)
    {
        if (idx < text.length())
        {
            const QChar c = text.at(idx);
            if (c == ' ' || c == '\t')
            {
                if (runStart < 0)
                    runStart = idx;
                continue;
            }
        }

        if (runStart < 0)
            continue;

        // Within this highlighter, a run of consecutive spaces/tabs is
        // always covered either by a single atomic setFormat() call
        // (a whole string/quoted-identifier/comment/completed keyword
        // phrase) or by none at all - formatting never changes mid-run.
        // So it's enough to sample the format of the first character
        // and apply it to the entire run in one call.
        QTextCharFormat fmt = format(runStart);
        QColor fg = fmt.foreground().style() != Qt::NoBrush
                        ? fmt.foreground().color()
                        : QApplication::palette().color(QPalette::Text);
        fg.setAlpha(kWhitespaceMarkerAlpha);
        fmt.setForeground(fg);
        setFormat(runStart, idx - runStart, fmt);
        runStart = -1;
    }
}
