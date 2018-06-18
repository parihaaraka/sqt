#ifndef FINDANDREPLACEPANEL_H
#define FINDANDREPLACEPANEL_H

#include <QWidget>
#include <QLineEdit>
#include <QTextCursor>
#include <QRegularExpression>

class QPlainTextEdit;
class QueryWidget;

namespace Ui {
class FindAndReplacePanel;
}

class FindAndReplacePanel : public QWidget
{
    Q_OBJECT
    
public:
    explicit FindAndReplacePanel(QWidget *parent = nullptr);
    ~FindAndReplacePanel();
    QList<QAction*> actions();
    void setEditor(QueryWidget *qw);
    void refreshActions();
    
private slots:
    void findTriggered();
    void on_actionReplace_and_find_next_triggered();
    void on_cbRegexp_toggled(bool checked);
    void on_btnReplace_clicked();
    void on_btnReplaceAll_clicked();

private:
    enum FindMode {Forward, Backward, Check};
    Ui::FindAndReplacePanel *ui;
    QueryWidget *_queryWidget;
    QTextCursor find(const QTextCursor &cursor = QTextCursor(), bool *nextPass = nullptr, FindMode mode = Forward);
    QRegularExpression::PatternOptions regexpOptions();
    QString unescape(QString ui_string, bool *err);

protected:
    bool eventFilter(QObject *target, QEvent *event);
};

#endif // FINDANDREPLACEPANEL_H
