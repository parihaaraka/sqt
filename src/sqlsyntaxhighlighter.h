#ifndef SQLSYNTAXHIGHLIGHTER_H
#define SQLSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>

class SqlSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SqlSyntaxHighlighter(const QJsonObject &settings, QObject *parent = 0);

protected:
    virtual void highlightBlock(const QString &text);

private:
    enum class LastWordOption { Yes, No, MayBe };
    struct WordInfo
    {
        char formatIndex;
        LastWordOption isLastWord;
        QHash <QString, WordInfo> nextWords;
    };
    QHash <QString, WordInfo> keywords;

    QVector<QTextCharFormat> formats;
    QHash<QString, char> functions;
    QString delimiters;
    bool tsqlBrackets;
};

#endif // SQLSYNTAXHIGHLIGHTER_H
