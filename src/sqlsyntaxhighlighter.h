#ifndef SQLSYNTAXHIGHLIGHTER_H
#define SQLSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>
#include <memory>
#include "sqllexer.h"

class SqlSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SqlSyntaxHighlighter(const QJsonDocument &settings, QObject *parent = nullptr);
    explicit SqlSyntaxHighlighter(std::shared_ptr<const SqlLexer> lexer,
                                  const QJsonDocument &settings,
                                  QObject *parent = nullptr);
    bool isKeyword(const QString &word);

protected:
    virtual void highlightBlock(const QString &text);

private:
    void initFormats(const QJsonDocument &settings);

    std::shared_ptr<const SqlLexer> _lexer;
    /// formats to paint the scanner's tokens with (\see SqlLexer::Token)
    QVector<QTextCharFormat> formats;
    /// formats of the `keyword` partitions (\see SqlLexer::Span::group)
    QVector<QTextCharFormat> keywordFormats;
    QTextCharFormat mixedEncodingFormat;
    void fadeWhitespaceMarkers(const QString &text);
};

#endif // SQLSYNTAXHIGHLIGHTER_H
