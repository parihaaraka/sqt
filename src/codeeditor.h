#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>
#include <QTextBlockUserData>

class CodeBlockProperties;
class QCompleter;
class CodeEditor;

namespace Bookmarks
{
    CodeBlockProperties* next();
    CodeBlockProperties* previous();
    CodeBlockProperties* last();
    void remove(CodeBlockProperties* item);
    void suspend();
    void resume();
}

class CodeBlockProperties : public QTextBlockUserData
{
public:
    CodeBlockProperties(CodeEditor *editor, CodeBlockProperties *toReplace);
    CodeBlockProperties(CodeEditor *editor);
    ~CodeBlockProperties();
    CodeEditor* editor() const;
private:
    // bool _isBookmarked = true;
    CodeEditor *_editor;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    CodeEditor(QWidget *parent = nullptr);
    void leftSideBarPaintEvent(QPaintEvent *event);
    int leftSideBarWidth() const;
    QString text() const;
    void setCompleter(QCompleter *completer);

protected:
    void resizeEvent(QResizeEvent *event) override;
    virtual bool eventFilter(QObject *object, QEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *e) override;
    void insertFromMimeData(const QMimeData *source) override;

private slots:
    void updateLeftSideBarWidth();
    void updateLeftSideBar(const QRect &rect, int dy);
    void onHlTimerTimeout();
    void insertCompletion(const QString &completion);

private:
    QList<QTextEdit::ExtraSelection> matchBracket(QString &docContent, const QTextCursor &selectedBracket, int darkerFactor = 100) const;
    QList<QTextEdit::ExtraSelection> currentLineSelection() const;
    bool isEnveloped(int pos) const;
    QWidget *_leftSideBar;
    QTimer *_hlTimer;
    QCompleter *_completer = nullptr;

signals:
    void completerRequest();
};

#endif // CODEEDITOR_H
