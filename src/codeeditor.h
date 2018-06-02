#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>
#include <QTextBlockUserData>

class CodeBlockProperties;
namespace Bookmarks
{
    CodeBlockProperties* next();
    CodeBlockProperties* previous();
    CodeBlockProperties* last();
}

class QueryWidget;
class CodeBlockProperties : public QTextBlockUserData
{
public:
    CodeBlockProperties(QueryWidget *w);
    ~CodeBlockProperties();
    QueryWidget* queryWidget() const;
private:
    bool _isBookmarked = true;
    QueryWidget *_w;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    CodeEditor(QWidget *parent = 0);
    void leftSideBarPaintEvent(QPaintEvent *event);
    int leftSideBarWidth();

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;

private slots:
    void updateLeftSideBarWidth();
    void updateLeftSideBar(const QRect &rect, int dy);
    void onHlTimerTimeout();

private:
    QList<QTextEdit::ExtraSelection> matchBracket(QString &docContent, const QTextCursor &selectedBracket, int darkerFactor = 100);
    QList<QTextEdit::ExtraSelection> currentLineSelection();
    bool isEnveloped(int pos);
    QWidget *_leftSideBar;
    QTimer *_hlTimer;
};

#endif // CODEEDITOR_H
