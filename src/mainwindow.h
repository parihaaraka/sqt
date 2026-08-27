#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include "extfiledialog.h"
#include <QItemSelection>
#include <QLabel>
#include <memory>
#include <optional>
#include "dbconnection.h"
#include "filesearch.h"     // FileSearchHit travels through the slots below

namespace Ui {
class MainWindow;
}

namespace Scripting {
class CppConductor;
}

class QAction;
class SqlSyntaxHighlighter;
class QueryWidget;
class TableModel;
class FindAndReplacePanel;
class DbObjectsModel;
class CodeBlockProperties;
class MyProxyStyle;
class FileSearchPanel;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    virtual ~MainWindow() override;
    void activateEditorBlock(CodeBlockProperties *blockProperties);
    void queryStateChanged(QueryWidget *w, QueryState state);
    QueryWidget* openScriptTab(const QString &text, const QString &title, DbConnection *connection = nullptr);
    /// What on_actionExecute_query_triggered() does, minus the cancel/stop-
    /// timer branch, so QueryWidget's Ctrl+Return handler can reuse it. With
    /// currentStatementOnly and no selection, only the top-level SQL
    /// statement under the caret is sent - see SqlLexer::statementBounds().
    void executeQuery(QueryWidget *q, bool currentStatementOnly);

protected:
    virtual void closeEvent(QCloseEvent *event) override;
    virtual void changeEvent(QEvent *e) override;

private slots:
    void on_addConnectionAction_triggered();
    void on_actionAbout_triggered();
    void on_objectsView_activated(const QModelIndex &index);
    void on_objectsView_customContextMenuRequested(const QPoint &pos);
    void on_actionRefresh_triggered();
    void on_actionChange_sort_mode_triggered();
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void currentChanged(const QModelIndex &current, const QModelIndex &previous);
    void viewModeActionTriggered(QAction *action);
    void on_actionExecute_query_triggered();
    void on_actionNew_triggered();
    void on_tabWidget_tabCloseRequested(int index);
    void sqlChanged();
    void on_actionOpen_triggered();
    void on_actionSave_triggered();
    void on_actionSave_as_triggered();
    void scriptSelectedObjects();
    void showContent(QModelIndex &index, const Scripting::CppConductor *content);
    void showTextualContent(const QVariant &value, const QVariant &type, std::shared_ptr<DbConnection> con);
    void objectsViewAdjustColumnWidth(const QModelIndex &);
    void on_actionFind_triggered();
    void on_tabWidget_currentChanged(int index);
    void onActionOpenFile();
    void openFile(const QString &fileName, const QString &encoding);

    void on_actionSettings_triggered();
    /// Ctrl+Shift+F: raises the search tab, seeds it with the selected text or
    /// the word under the cursor and remembers the connection of the widget the
    /// shortcut came from - files are highlighted and opened with that one.
    void on_actionFind_in_files_triggered();
    /// Ctrl+Shift+O: back to the object tree, with the focus in it. The
    /// counterpart of Ctrl+Shift+F, so that the two tabs of the left pane can be
    /// swapped without reaching for the mouse.
    void on_actionObject_tree_triggered();
    /// Shows the place \a hit points at in the content pane, as sql.
    void previewFileHit(const FileSearchHit &hit, bool focusPane);
    /// Refreshes the content pane from whichever of the two left tabs is up: the
    /// tree's current object, or the search's current hit. F2 and a focus change
    /// go through this rather than straight to scriptSelectedObjects(), which
    /// would put the tree's object on screen while the search results are shown.
    void refreshContentPane();
    /// Ctrl+E over the results: an editor tab with the file, cursor on the hit.
    void openFileHitInEditor(const FileSearchHit &hit);
    /// Ctrl+E with the focus in the preview pane: the very file the pane shows,
    /// opened at the pane's own cursor rather than at the hit the preview
    /// started from. Reading a found place usually means walking away from it,
    /// and it is the place being read that the editor is wanted for.
    void openPaneFileInEditor();

    /// Points the locator at the current assets directory and drops everything
    /// cached on top of it: the scripts, the keyword dictionaries, the tree
    /// icons and the branch arrows. Nothing is reopened and no tab is closed.
    void reloadAssets();


private:
    QLabel _contextLabel, _positionLabel, _durationLabel;
    ExtFileDialog _fileDialog;
    QStringList _mruDirs; // QFileDialog::history() keeps unused directories :(
    Ui::MainWindow *ui;
    MyProxyStyle *_proxyStyle;
    DbObjectsModel *_objectsModel;
    TableModel *_tableModel;
    QueryWidget* currentQueryWidget();
    QueryWidget* _objectScript;
    // tab under cursor while the tab bar context menu is up, -1 otherwise
    int _menuTabIndex = -1;
    // separator + per-file actions, shown only when the target tab has a file
    QList<QAction*> _fileTabActions;
    int targetTabIndex() const;
    // tab captions: composed in a single place, so that the tabs having no file
    // behind them do not lose their names on the first edit
    void updateTabCaption(QueryWidget *w);
    QString autoTabTitle(const QueryWidget *w) const;
    void retitleOnDatabaseChange(QueryWidget *w);
    /// Closes the link of a database node that is not expanded in the tree.
    /// Selecting such a node does need a connection - its content and preview
    /// scripts run on it - but nothing after that does, so the link is given
    /// back instead of being left behind for the rest of the session. A live
    /// link belongs to an expanded branch only. The connection object itself
    /// stays registered, so the node keeps its place, its indicator merely
    /// turns red, and the next click opens the link again.
    /// \a srcIndex belongs to the source model; the owner of the link is its
    /// nearest "database" ancestor (or itself), exactly as dbConnection() sees
    /// it. A top level "connection" node is left alone: those are opened and
    /// closed by the user explicitly.
    void releaseIdleDatabaseConnection(const QModelIndex &srcIndex);

    bool closeTab(int index);
    bool ensureSaved(int index, bool ask_name = false, bool forceWarning = false);
    FindAndReplacePanel *_frPanel;
    // Created in the constructor, but the Ctrl+E lambda above is installed
    // before that, so it must be safe to test.
    FileSearchPanel *_searchPanel = nullptr;
    /// The connection the file search works against: a private clone of the one
    /// that was current when Ctrl+Shift+F was pressed. A script on disk belongs
    /// to a database in the user's head, and that is the database whose
    /// dictionary should highlight it - not whichever node the tree happens to
    /// point at later.
    ///
    /// Owned rather than borrowed, and for a reason: a weak_ptr here expired
    /// behind the user's back on every ordinary turn of events - collapsing or
    /// refreshing a database node destroys its DbObject, whose destructor
    /// unregisters the connection, and closing the tab the link came from
    /// deletes that tab's clone. The search then lost its dbms, hence its
    /// highlighting dictionary, hence the ability to open a found file as
    /// anything but plain text. A clone keeps the connection string, the
    /// database and (see PgConnection::clone()) the dbms identity, so all of
    /// that survives even a server that has gone away. It is never opened just
    /// to colour a file - naming the script bundle needs no link - so holding it
    /// costs no backend.
    std::shared_ptr<DbConnection> _searchConnection;
    /// The place the content pane is showing, while it shows a file rather than
    /// the script of a tree node. Coming back to the tree rebuilds the pane only
    /// then: scriptSelectedObjects() reruns the node's content script and
    /// reopens a link that has just been given back, which is too much for a
    /// mere tab switch. Ctrl+E in the pane needs the same knowledge - which file
    /// is on screen - and would otherwise open the tree's object instead.
    std::optional<FileSearchHit> _paneHit;
    /// Identity of \a con for the search panel's per-connection settings, plus
    /// the readable \a label to store next to them. The key is a digest of the
    /// connection string with the password removed - stable across restarts,
    /// while a password (or a database switched inside the session) leaves it
    /// alone, and nothing secret reaches the settings file.
    static QString searchProfileKey(const std::shared_ptr<DbConnection> &con,
                                    QString *label = nullptr);
    /// Positions \a w on \a line / \a column (1-based) and centers the view.
    /// \a matchColor marks the range as a search hit (see
    /// CodeEditor::setMatchHighlight): the text cursor's own selection is not
    /// enough, since it is painted with the palette's Inactive group while the
    /// focus stays in the results tree.
    static void gotoFilePosition(QueryWidget *w, int line, int column, int length,
                                 const QColor &matchColor = QColor());
    QTimer *_hideTimer;
    QTimer *_durationRefreshTimer;
    void log(const QString &msg);
    void adjustMru();
    void addMruFile();

public slots:
    QVariant current(const QString &nodeType, const QString &field);
    QVariantList selected(const QString &nodeType, const QString &field);
    virtual bool eventFilter(QObject *object, QEvent *event) override;

    void refreshActions();
    void refreshContextInfo();
    void refreshCursorInfo();
    void refreshConnectionState();
    void onMessage(const QString &msg);
    void onError(const QString &err);
};

#endif // MAINWINDOW_H
