#ifndef QUERYWIDGET_H
#define QUERYWIDGET_H

#include <QWidget>
#include <QSplitter>
#include <QTextCursor>
#include <QPlainTextEdit>
#include <memory>

class DbConnection;
class TableModel;
class SqlSyntaxHighlighter;
class QMenu;
class FindAndReplacePanel;
class QVBoxLayout;
class DataTable;

class QueryWidget : public QSplitter
{
    Q_OBJECT
public:
    explicit QueryWidget(QWidget *parent = 0);
    explicit QueryWidget(DbConnection *connection, QWidget *parent = 0);
    ~QueryWidget();
    const QString& fileName() { return _fn; }
    bool openFile(const QString &fileName, const QString &encoding);
    bool saveFile(const QString &fileName, const QString &encoding = QString());
    QString encoding() { return _encoding; }
    DbConnection* dbConnection() { return _connection.get(); }
    void setDbConnection(DbConnection *connection);
    void ShowFindPanel(FindAndReplacePanel *panel);
    void highlight(std::shared_ptr<DbConnection> con = nullptr);
    void dehighlight();

    void setReadOnly(bool ro = true);
    bool isReadOnly() const;
    void clear();
    bool isModified() const;
    void setModified(bool m = true);
    void setTextCursor(const QTextCursor &cursor);
    QString toPlainText();
    QTextCursor textCursor() const;
    QTextDocument* document() const;
    QWidget* editor() const;
    void setPlainText(const QString &text);
    void setHtml(const QString &html);

signals:
    void sqlChanged();
    void error(QString msg) const;

public slots:
    void onMessage(const QString &text);
    void onError(const QString &text);
    void fetched(DataTable *table);
    void clearResult();
    //void onCustomGridContextMenuRequested(const QPoint & pos);
    //void on_customEditorContextMenuRequested(const QPoint & pos);

private:
    QString _fn;
    QString _encoding;
    QWidget *_editor;
    QPlainTextEdit *_messages;
    QSplitter *_resSplitter;
    std::shared_ptr<DbConnection> _connection;
    SqlSyntaxHighlighter *_highlighter;
    QVBoxLayout *_editorLayout;
    QList<TableModel*> _tables;
    QMenu *_resultMenu;
    QAction *_actionCopy;
    bool eventFilter(QObject *object, QEvent *event);
    void log(const QString &text, QColor color);
};

#endif // QUERYWIDGET_H
