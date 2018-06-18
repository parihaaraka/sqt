#ifndef JSONSYNTAXHIGHLIGHTER_H
#define JSONSYNTAXHIGHLIGHTER_H

#include <QSyntaxHighlighter>

class JsonSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
public:
    explicit JsonSyntaxHighlighter(QObject *parent = nullptr);

protected:
    virtual void highlightBlock(const QString &text);

private:
    const QString delimiters = " \t\r\n\f\b\":[]{},/";
    QVector<QTextCharFormat> formats;
};

#endif // JSONSYNTAXHIGHLIGHTER_H
