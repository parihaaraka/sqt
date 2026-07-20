#ifndef SQLSYNTAXHIGHLIGHTER_H
#define SQLSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>

class SqlSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit SqlSyntaxHighlighter(const QJsonDocument &settings, QObject *parent = nullptr);
    bool isKeyword(const QString &word);

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
    void fadeWhitespaceMarkers(const QString &text);
};

#endif // SQLSYNTAXHIGHLIGHTER_H
