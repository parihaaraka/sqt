#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "connectiondialog.h"
#include "settings.h"
#include <QMessageBox>
#include "dbobject.h"
#include "dbobjectsmodel.h"
#include "logindialog.h"
#include <QTimer>
#include "dbosortfilterproxymodel.h"
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include "querywidget.h"
#include <QTextDocumentFragment>
#include <QCloseEvent>
#include <QTextEdit>
#include <QToolButton>
#include "tablemodel.h"
#include "dbtreeitemdelegate.h"
#include "findandreplacepanel.h"
#include <memory>
#include "scripting.h"
#include "codeeditor.h"
#include <QScrollBar>
#include "settingsdialog.h"
#include "queryoptions.h"

struct RecentFile
{
    QString fileName;
    QString encoding;
};

Q_DECLARE_METATYPE(QList<RecentFile>)

QDataStream& operator<<(QDataStream& out, const QList<RecentFile> &fList)
{
    for (auto const &f: fList)
        out << f.fileName << f.encoding;
    return out;
}

QDataStream& operator>>(QDataStream& in, QList<RecentFile> &fList)
{
    while (!in.atEnd())
    {
        RecentFile f;
        in >> f.fileName >> f.encoding;
        if (!f.fileName.isEmpty())
            fList.push_back(f);
    }
    return in;
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    _proxyStyle = new MyProxyStyle();

    qRegisterMetaType<QueryState>();
    qRegisterMetaTypeStreamOperators<QList<RecentFile>>("RecentFile");
    SqtSettings::load();

    setCentralWidget(ui->splitterV);
    ui->statusBar->addPermanentWidget(&_contextLabel);
    _positionLabel.setVisible(false);
    ui->statusBar->addPermanentWidget(&_positionLabel);
    ui->statusBar->addPermanentWidget(&_durationLabel);

#ifndef Q_OS_WIN
    _contextLabel.setFrameStyle(QFrame::StyledPanel);
    _positionLabel.setFrameStyle(QFrame::StyledPanel);
#endif

    _objectScript = new QueryWidget(this);
    _objectScript->setReadOnly(true);
    ui->contentSplitter->insertWidget(0, _objectScript);
    ui->objectsView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->objectsView->setItemDelegateForColumn(0, new DbTreeItemDelegate(this));
    ui->objectsView->setStyle(_proxyStyle);
    connect(ui->objectsView, &QTreeView::expanded, this, &MainWindow::objectsViewAdjustColumnWidth);
    connect(ui->objectsView, &QTreeView::collapsed, [this](const QModelIndex &index) {
        auto model = (index.isValid() ? qobject_cast<const QSortFilterProxyModel*>(index.model()) : nullptr);
        if (!model) // i saw index being invalid on windows (wtf?)
            return;
        const QModelIndex currentNodeIndex = model->mapToSource(index);
        DbObject *obj = static_cast<DbObject*>(currentNodeIndex.internalPointer());
        // close db connection on database node collapse
        if (obj && obj->data(DbObject::TypeRole).toString() == "database")
        {
            auto con = DbConnectionFactory::connection(QString::number(std::intptr_t(obj)));
            if (con)
                con->close();
        }
        objectsViewAdjustColumnWidth(index);
    });

    DboSortFilterProxyModel *proxyModel = new DboSortFilterProxyModel(this);
    proxyModel->sort(0, Qt::AscendingOrder);
    _objectsModel = new DbObjectsModel();
    connect(_objectsModel, &DbObjectsModel::error, this, &MainWindow::onError);
    connect(_objectsModel, &DbObjectsModel::message, this, &MainWindow::onMessage);

    proxyModel->setSourceModel(_objectsModel);
    ui->objectsView->setModel(proxyModel);
    connect(ui->objectsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::selectionChanged);
    connect(ui->objectsView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::currentChanged);

    _durationRefreshTimer = new QTimer(this);
    connect(_durationRefreshTimer, &QTimer::timeout, this, &MainWindow::refreshConnectionState);
    _durationRefreshTimer->start(200);

    // it's about messages frame auto-hide
    _hideTimer = new QTimer(this);
    _hideTimer->setSingleShot(true);
    _hideTimer->setInterval(6000);
    ui->splitterV->setSizes({1000, 0});
    connect(ui->splitterV, &QSplitter::splitterMoved, [this]() { _hideTimer->stop(); });
    connect(_hideTimer, &QTimer::timeout, [this](){
        ui->splitterV->setSizes({1000, 0});
    });

    ui->actionRefresh->setShortcuts(QKeySequence::Refresh);
    ui->actionQuit->setShortcuts(QKeySequence::Quit);
    ui->actionNew->setShortcuts(QKeySequence::New);
    ui->actionOpen->setShortcuts(QKeySequence::Open);
    ui->actionSave->setShortcuts(QKeySequence::Save);
    ui->actionSave_as->setShortcuts(QKeySequence::SaveAs);
    ui->actionFind->setShortcuts(QKeySequence::Find);

    QAction *editAction = new QAction(ui->objectsView);
    editAction->setShortcut({"Ctrl+E"});
    connect(editAction, &QAction::triggered, this, &MainWindow::on_actionNew_triggered);
    ui->objectsView->addAction(editAction);

    _frPanel = new FindAndReplacePanel();
    ui->menuEdit->addActions(_frPanel->actions());

    ui->objectsView->installEventFilter(this);
    refreshActions();

    _tableModel = new TableModel(this);
    ui->tableView->setModel(_tableModel);
    ui->tableView->hide();
    //ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QActionGroup *viewMode = new QActionGroup(this);
    viewMode->addAction(ui->actionObject_content);
    viewMode->addAction(ui->actionQuery_editor);
    connect(viewMode, &QActionGroup::triggered, this, &MainWindow::viewModeActionTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
    ui->actionObject_content->setChecked(true);
    viewModeActionTriggered(ui->actionObject_content);

    // refresh root content (connection nodes from settings)
    _objectsModel->fillChildren();

    restoreGeometry(SqtSettings::value("mainWindowGeometry").toByteArray());
    restoreState(SqtSettings::value("mainWindowState").toByteArray());
    QList<RecentFile> fList = SqtSettings::value("recentFiles").value<QList<RecentFile>>();
    for (const auto &f: fList)
    {
        QAction *a = ui->menuOpen_recent->addAction(f.fileName);
        a->setData(f.encoding);
        connect(a, &QAction::triggered, this, &MainWindow::onActionOpenFile);
    }

    ui->splitterH->restoreState(SqtSettings::value("mainWindowHSplitter").toByteArray());
    ui->contentSplitter->restoreState(SqtSettings::value("mainWindowScriptSplitter").toByteArray());
    _fileDialog.restoreState(SqtSettings::value("fileDialog").toByteArray());
    _mruDirs = SqtSettings::value("mruDirs").toStringList();
    // make previously used directory to be current
    if (!_mruDirs.isEmpty())
        _fileDialog.setDirectory(_mruDirs.last());

    // switch to next tab page
    QAction *tabWidgetAction = new QAction(tr("next page"), ui->tabWidget);
    tabWidgetAction->setShortcut(QKeySequence::Forward);
    connect(tabWidgetAction, &QAction::triggered, [this]()
    {
        if (ui->tabWidget->currentIndex() < ui->tabWidget->count() - 1)
            ui->tabWidget->setCurrentIndex(ui->tabWidget->currentIndex() + 1);
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);

    // switch to previous tab page
    tabWidgetAction = new QAction(tr("previous page"), ui->tabWidget);
    tabWidgetAction->setShortcut(QKeySequence::Back);
    connect(tabWidgetAction, &QAction::triggered, [this]()
    {
        if (ui->tabWidget->currentIndex() > 0)
            ui->tabWidget->setCurrentIndex(ui->tabWidget->currentIndex() - 1);
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);

    // switch to previous tab page
    tabWidgetAction = new QAction(tr("close page"), ui->tabWidget);
    tabWidgetAction->setShortcut(QKeySequence::Close);
    connect(tabWidgetAction, &QAction::triggered, [this]()
    {
        if (ui->tabWidget->currentIndex() >= 0)
            closeTab(ui->tabWidget->currentIndex());
        QWidget *w = ui->tabWidget->currentWidget();
        if (w)
            w->setFocus();
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);
    ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::ActionsContextMenu);

    // corner buttons
    QWidget *cornerWidget = new QWidget();
    QHBoxLayout *l = new QHBoxLayout();
    l->setContentsMargins(2, 0, 2, 4);
    QToolButton *dbBtn = new QToolButton(ui->tabWidget);
    dbBtn->setAutoRaise(true);
    QMenu *dbBtnMenu = new QMenu(dbBtn);
    connect(dbBtnMenu, &QMenu::aboutToShow, [this, dbBtnMenu](){
        dbBtnMenu->clear();
        QueryWidget *qw = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
        DbConnection *currentConnection = (qw ? qw->dbConnection() : nullptr);
        auto m = ui->objectsView->model();
        for (int cr = 0; cr < m->rowCount(); ++cr) // iterate connections
        {
            QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(m)->mapToSource(m->index(cr, 0));
            DbConnection *connection = _objectsModel->dbConnection(srcIndex).get();
            if (connection && connection->isOpened())
            {
                // collect databases on next level only
                // (if somebody build folders of databases, then databases will not be found)
                QStringList databases;
                for (int dr = 0; dr < m->rowCount(m->index(cr, 0)); ++dr)
                {
                    QModelIndex ind = m->index(dr, 0, m->index(cr, 0));
                    if (ind.data(DbObject::TypeRole).toString() != "database")
                        continue;
                    QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(m)->mapToSource(ind);
                    DbConnection *connection = _objectsModel->dbConnection(srcIndex).get();
                    databases.append(connection->database());
                }

                if (databases.isEmpty())
                {
                    dbBtnMenu->addAction(srcIndex.data().toString(), [this, qw, connection](){
                        DbConnection *cn = connection->clone();
                        qw->setDbConnection(cn);
                        refreshContextInfo();
                    });
                }
                else if (currentConnection && connection->connectionString() == currentConnection->connectionString())
                {
                    // put databases of current connection on top level
                    QAction *next = dbBtnMenu->actions().isEmpty() ? nullptr : dbBtnMenu->actions().first();
                    QList<QAction*> actions;
                    for (int i = 0; i < databases.size(); ++i)
                    {
                        if (currentConnection->database() != databases[i])
                        {
                            QAction *a = new QAction(databases[i], dbBtnMenu);
                            connect(a, &QAction::triggered, [this, a, currentConnection](){
                                currentConnection->setDatabase(a->text());
                                currentConnection->open();
                                refreshContextInfo();
                            });
                            actions.append(a);
                        }
                    }
                    dbBtnMenu->insertActions(next, actions);
                    dbBtnMenu->insertSeparator(next);
                }
                else
                {
                    QMenu *menu = dbBtnMenu->addMenu(srcIndex.data().toString());
                    for (const QString &db: databases)
                        menu->addAction(db, [this, qw, connection, db](){
                            DbConnection *cn = connection->clone();
                            cn->setDatabase(db);
                            qw->setDbConnection(cn);
                            refreshContextInfo();
                        });
                }
            }
        }
    });
    dbBtn->setMenu(dbBtnMenu);
    // don't use QToolButton::InstantPopup to use QAction's shortcut
    // scope Qt::WidgetWithChildrenShortcut instead of manual handling
    QAction *dbBtnAction = new QAction(ui->tabWidget);
    dbBtnAction->setIcon(QIcon(":img/databases.png"));
    dbBtnAction->setShortcut({"Ctrl+D"});
    dbBtnAction->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(dbBtnAction, &QAction::triggered, dbBtn, &QToolButton::showMenu);
    dbBtn->setDefaultAction(dbBtnAction);
    dbBtn->setToolTip(tr("switch to another accessible database") +
                      "<br/><b>" + dbBtnAction->shortcut().toString() + "</b>");
    ui->tabWidget->addAction(dbBtnAction);
    l->addWidget(dbBtn);
    cornerWidget->setLayout(l);
    ui->tabWidget->setCornerWidget(cornerWidget);//, Qt::TopLeftCorner);
}

MainWindow::~MainWindow()
{
    delete _frPanel;
    delete _objectsModel;
    delete ui;
    delete _proxyStyle;
}

void MainWindow::activateEditorBlock(CodeBlockProperties *blockProperties)
{
    if (!blockProperties)
        return;

    // extract useful knowledge from CodeBlockProperties context
    QueryWidget *w = nullptr;
    QObject *object = blockProperties->editor();
    while (object)
    {
        w = qobject_cast<QueryWidget*>(object);
        if (w)
            break;
        object = object->parent();
    }
    if (!w)
        return;

    QTextDocument *doc = qobject_cast<QTextDocument*>(w->document());
    QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(w->editor());
    if (!doc || !ed)
        return;

    QTextBlock block = doc->begin();
    while (block.isValid())
    {
        if (block.userData() == blockProperties)
        {
            if (ui->tabWidget->isHidden())
                ui->actionQuery_editor->trigger();
            ui->tabWidget->setCurrentWidget(w);

            QTextCursor cursor(block);
            w->setTextCursor(cursor);
            ed->centerCursor();
            break;
        }
        block = block.next();
    }
}

void MainWindow::queryStateChanged(QueryWidget *w, QueryState state)
{
    if (ui->tabWidget->currentWidget() != w)
        return;

    refreshActions();
    if (state == QueryState::Inactive && !w->isTimerActive())
        refreshContextInfo();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    while (ui->tabWidget->count() > 0)
    {
        if (!closeTab(ui->tabWidget->currentIndex()))
        {
            event->ignore();
            return;
        }
    }
    event->accept();
    SqtSettings::setValue("mainWindowGeometry", saveGeometry());
    SqtSettings::setValue("mainWindowState", saveState());
    SqtSettings::setValue("mainWindowHSplitter", ui->splitterH->saveState());
    SqtSettings::setValue("mainWindowScriptSplitter", ui->contentSplitter->saveState());
    SqtSettings::setValue("fileDialog", _fileDialog.saveState());
    SqtSettings::setValue("mruDirs", _mruDirs);
}

void MainWindow::changeEvent(QEvent *e)
{
    QMainWindow::changeEvent(e);
    switch (e->type()) {
    case QEvent::LanguageChange:
        ui->retranslateUi(this);
        break;
    default:
        break;
    }
}

void MainWindow::on_addConnectionAction_triggered()
{
    ConnectionDialog *dlg = new ConnectionDialog(this);
    if (dlg->exec() == QDialog::Accepted)
        _objectsModel->addServer(dlg->name(), dlg->connectionString());
    delete dlg;
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, tr("About"),
                       tr("sql query tool v%1<br/>&copy; 2013-2020 Andrey Lukyanov<br/>freeware<br/><br/>"
                          "The program is provided AS IS with NO WARRANTY OF ANY "
                          "KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY "
                          "AND FITNESS FOR A PARTICULAR PURPOSE.<br/><br/>"
                          "Some icons by <a href='http://p.yusukekamiyamane.com'>Yusuke Kamiyamane</a>. All rights reserved.").
                       arg(QApplication::applicationVersion()));
}

void MainWindow::on_objectsView_activated(const QModelIndex &index)
{
    QModelIndex currentNodeIndex = qobject_cast<QSortFilterProxyModel*>(ui->objectsView->model())->mapToSource(index);
    DbObject *obj = static_cast<DbObject*>(currentNodeIndex.internalPointer());
    if (obj->data(DbObject::TypeRole).toString() == "connection" &&
            !obj->data(DbObject::ParentRole).toBool()) // not expanded => not connected yet
    {
        QString cs = obj->data(DbObject::DataRole).toString();
        QString user = obj->data(DbObject::NameRole).isNull() ? "" : obj->data(DbObject::NameRole).toString();
        QString newUser;
        if (cs.contains("%pass%"))
        {
            std::unique_ptr<LoginDialog> dlg(new LoginDialog(this, cs.contains("%user%") ? user : QString()));
            dlg->setWindowTitle(tr("Connect to %1").arg(obj->data().toString()));
            if (dlg->exec() != QDialog::Accepted)
                return;
            newUser = dlg->user();
            cs = cs.replace("%user%", dlg->user(), Qt::CaseInsensitive).
                    replace("%pass%", dlg->password(), Qt::CaseInsensitive);
        }

        QString connectionID = QString::number(std::intptr_t(obj));
        std::shared_ptr<DbConnection> con = DbConnectionFactory::createConnection(QString::number(std::intptr_t(obj)), cs);
        connect(con.get(), &DbConnection::error, this, &MainWindow::onError);
        connect(con.get(), &DbConnection::message, this, &MainWindow::onMessage);
        if (!con->open())
        {
            con->disconnect();
            DbConnectionFactory::removeConnection(connectionID);
            return;
        }
        else
        {
            if (user != newUser && !newUser.isEmpty())
            {
                obj->setData(newUser, DbObject::NameRole);
                _objectsModel->saveConnectionSettings();
            }
            //con->disconnect(errConnection);
            scriptSelectedObjects();
            _objectsModel->setData(currentNodeIndex, true, DbObject::ParentRole);
        }
    }
    // index belongs to proxy model
    ui->objectsView->expand(index);
}

void MainWindow::on_objectsView_customContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = ui->objectsView->indexAt(pos);
    QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->mapToSource(index);
    QMenu myMenu;
    QAction *actionModify = nullptr;
    QAction *actionDelete = nullptr;
    QAction *actionConnect = nullptr;
    std::shared_ptr<DbConnection> con;
    if (_objectsModel->data(srcIndex, DbObject::TypeRole) == "connection")
    {
        con = _objectsModel->dbConnection(srcIndex);
        actionConnect = myMenu.addAction(con && con->isOpened() ? tr("Disconnect") : tr("Connect"));
        myMenu.addSeparator();
        actionModify = myMenu.addAction(tr("Modify"));
        actionDelete = myMenu.addAction(tr("Delete"));
    }
    if (myMenu.actions().isEmpty())
        return;

    QAction* selectedItem = myMenu.exec(ui->objectsView->mapToGlobal(pos));
    if (selectedItem == actionModify)
    {
        std::unique_ptr<ConnectionDialog> dlg(
                    new ConnectionDialog(
                        this,
                        _objectsModel->data(srcIndex, Qt::DisplayRole).toString(),
                        _objectsModel->data(srcIndex, DbObject::DataRole).toString())
                    );
        if (dlg->exec() == QDialog::Accepted)
        {
            // disconnect here ?
            _objectsModel->alterConnection(srcIndex, dlg->name(), dlg->connectionString());
        }
    }
    else if (selectedItem == actionDelete)
    {
        int ret = QMessageBox::warning(
                    this,
                    tr("Warning"),
                    tr("Delete selected connection?"),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::No
                    );
        if (ret == QMessageBox::Yes)
            _objectsModel->removeConnection(srcIndex);
    }
    else if (selectedItem == actionConnect)
    {
        bool wasOpened = false;
        if (con) // "disconnect" in any case (clear tree)
        {
            wasOpened = con->isOpened();
            // there are problems with expanding desolated node without the following line
            ui->objectsView->collapse(index);

            DbObject *item = static_cast<DbObject*>(srcIndex.internalPointer());
            _objectsModel->removeRows(0, item->childCount(), srcIndex);
            con->close();
            con->disconnect(); // disconnect all slots from all signals
            DbConnectionFactory::removeConnection(QString::number(std::intptr_t(item)));
            _objectsModel->setData(srcIndex, false, DbObject::ParentRole);
            _objectsModel->setData(srcIndex, QVariant(), DbObject::ContentRole);
            showContent(srcIndex, nullptr);
        }
        if (!wasOpened)
            on_objectsView_activated(index);
    }
}

void MainWindow::on_actionRefresh_triggered()
{
    QModelIndex i = ui->objectsView->selectionModel()->currentIndex();
    if (!i.isValid())
        return;

    QModelIndex nodeToRefresh = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->mapToSource(i);
    DbObject *item = static_cast<DbObject*>(nodeToRefresh.internalPointer());

    // invalidate scripts cache to avoid reopening sqt on scripts change
    try
    {
        DbConnection *cn = _objectsModel->dbConnection(nodeToRefresh).get();
        Scripting::refresh(cn, Scripting::Context::Root);
        Scripting::refresh(cn, Scripting::Context::Content);
        Scripting::refresh(cn, Scripting::Context::Preview);
        Scripting::refresh(cn, Scripting::Context::Autocomplete);

        Scripting::refresh(cn, Scripting::Context::Tree);
    }
    catch (const QString &err)
    {
        onError(err);
    }
    catch (const std::runtime_error &e)
    {
        onError(QString::fromStdString(e.what()));
    }

    // clear all child nodes
    _objectsModel->removeRows(0, item->childCount(), nodeToRefresh);
    // force refresh
    _objectsModel->dataChanged(nodeToRefresh, nodeToRefresh);

    // clear current node's preserved data
    item->setData(QVariant(), DbObject::ContentRole);
    // refresh context part
    scriptSelectedObjects();
}

void MainWindow::on_actionChange_sort_mode_triggered()
{
    QWidget *fw = QApplication::focusWidget();
    if (fw != ui->objectsView)
        return;

    QModelIndex i = ui->objectsView->selectionModel()->currentIndex();
    if (!i.isValid())
        return;
    QSortFilterProxyModel *m = static_cast<QSortFilterProxyModel*>(ui->objectsView->model());
    QModelIndex srcIndex = m->mapToSource(i);
    DbObject *item = static_cast<DbObject*>(srcIndex.internalPointer());
    int sortData = item->data(DbObject::CurrentSortRole).toInt();
    item->setData(abs(sortData-1), DbObject::CurrentSortRole);
    m->invalidate();
}

void MainWindow::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(selected)
    Q_UNUSED(deselected)
    QItemSelectionModel *selectionModel = qobject_cast<QItemSelectionModel*>(sender());
    QModelIndexList si = selectionModel->selectedIndexes();
    QModelIndex cur = selectionModel->currentIndex();

    // We have to allow *single* parent for all selected nodes,
    // so the current node's parent is indicative one.

    bool allowMultiselect = (cur.parent().isValid() && cur.parent().data(DbObject::MultiselectRole).toBool());
    for (const QModelIndex &i: si)
    {
        if (
                // deselect nodes with different parent
                i.parent() != cur.parent() ||
                // deselect nodes if multiple selection is not allowed
                (i != cur && !allowMultiselect) ||
                // Deselect sibling selected "folders".
                // E.g. table may have the following children: column1, column2,.., Indexes, Triggers and so on.
                // Here "Indexes" and "Triggers" folders are not allowed to be selected along with columns.
                !i.data(DbObject::IdRole).isValid()
           )
            selectionModel->select(i, QItemSelectionModel::Deselect);
    }
    scriptSelectedObjects();
}

void MainWindow::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)
    ui->actionNew->setEnabled(current.isValid());
    ui->actionRefresh->setStatusTip(current.isValid() ? QTextEdit(current.data().toString()).toPlainText() : "");
    ui->actionRefresh->setEnabled(current.isValid());
    ui->actionChange_sort_mode->setEnabled(current.isValid());
    refreshContextInfo();
}

void MainWindow::viewModeActionTriggered(QAction *action)
{
    setUpdatesEnabled(false);
    ui->contentSplitter->setVisible(action == ui->actionObject_content);
    ui->tabWidget->setVisible(action == ui->actionQuery_editor);
    ui->actionObject_content->setShortcut(action == ui->actionQuery_editor ? Qt::Key_F2 : 0);
    ui->actionQuery_editor->setShortcut(action == ui->actionObject_content ? Qt::Key_F2 : 0);
    refreshConnectionState();
    setUpdatesEnabled(true);
    if (ui->tabWidget->isHidden())
        scriptSelectedObjects();
    else if (ui->tabWidget->currentWidget())
        ui->tabWidget->currentWidget()->setFocus();
}

void MainWindow::on_actionExecute_query_triggered()
{
    // TODO refactor this mash :/  (get db actions in QueryWidget ?)

    QueryWidget *q = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    DbConnection *con = q->dbConnection();
    if (!con)
        return;
    auto qState = con->queryState();
    if (qState == QueryState::Running || qState == QueryState::Cancelling || q->isTimerActive())
    {
        q->stopTimer();
        if (qState == QueryState::Inactive)
        {
            refreshActions();
            refreshContextInfo();
        }
        con->cancel();
    }
    else if (qState == QueryState::Inactive)
    {
        q->clearResult();
        QString query = (q->textCursor().hasSelection() ?
                             q->textCursor().selection().toPlainText() :
                             q->toPlainText());
        if (query.isEmpty())
            return;

        QJsonObject qSettings;
        // do not extract commented instructions from huge sql script
        if (query.size() < 1024 * 32)
        {
            qSettings = QueryOptions::Extract(query);
            int graphInterval = qSettings.contains("charts") ?
                        qSettings["interval"].toInt(-1) : -1;
            q->setQuerySettings(qSettings); // swap inside
            if (graphInterval > 0)
            {
                q->executeOnTimer(query, graphInterval);
                return;
            }
        }
        con->executeAsync(query);
    }
}

bool MainWindow::eventFilter(QObject *object, QEvent *event)
{
    Q_UNUSED(object)
    switch (event->type())
    {
    case QEvent::Show:
    case QEvent::Resize:
        if (object == ui->objectsView)
            objectsViewAdjustColumnWidth(QModelIndex());
        break;
    case QEvent::FocusIn:
        if (object->isWidgetType() && (object == ui->objectsView || !qobject_cast<QMenu*>(object)))
        {
            refreshActions();
            refreshContextInfo();
            refreshCursorInfo();
        }
        break;
    default: ; // to avoid a bunch of warnigs
    }
    return QMainWindow::eventFilter(object, event);
}

bool MainWindow::closeTab(int index)
{
    if (ensureSaved(index, false, true))
    {
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(index));

        DbConnection *con = w->dbConnection();
        if (con && con->queryState() != QueryState::Inactive)
        {
            onError(tr("connection is in use"));
            return false;
        }

        ui->tabWidget->removeTab(index);
        delete w;
        return true;
    }
    return false;
}

void MainWindow::on_actionNew_triggered()
{
    QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->
            mapToSource(ui->objectsView->currentIndex());
    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(srcIndex);
    if  (
            !con ||
            // do not try to open closed top-level connection
            (!con->isOpened() && srcIndex.data(DbObject::TypeRole).toString() == "connection") ||
            // open if not opened (shoud be database-level connection - it is closed when the node is collapsed)
            !con->open()
        )
    {
        onError(tr("db connection unavailable"));
        return;
    }

    QueryWidget *w;
    // create db-connected editor
    if (con->isOpened() || !con->database().isEmpty())
    {
        DbConnection *cn = con->clone();
        w = new QueryWidget(cn, ui->tabWidget);
    }
    // create editor without query execution ability
    // (broken by previous opened connection check)
    // * do we need it?
    else
    {
        w = new QueryWidget(ui->tabWidget);
    }

    int ind = ui->tabWidget->addTab(w, QString());
    ui->tabWidget->setCurrentIndex(ind);

    if (sender() != ui->actionNew && _objectsModel->data(srcIndex, DbObject::ContentTypeRole).toString() == "script")
        w->setPlainText(_objectsModel->data(srcIndex, DbObject::ContentRole).toString());
    else
        w->setPlainText("");

    w->highlight();
    connect(w, &QueryWidget::sqlChanged, this, &MainWindow::sqlChanged);
    w->setReadOnly(false);

    if (ui->contentSplitter->isVisible())
        ui->actionQuery_editor->activate(QAction::Trigger);
    w->setFocus();
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    closeTab(index);
}

void MainWindow::sqlChanged()
{
    QueryWidget *w = qobject_cast<QueryWidget*>(sender());
    int ind = ui->tabWidget->indexOf(w);
    if (ind == -1)
        return;
    QString caption = ui->tabWidget->tabText(ind);
    QString fn = QFileInfo(w->fileName()).fileName();
    bool isModified = w->isModified();
    bool captionMarked = caption.endsWith("*");
    if (!captionMarked && isModified)
    {
        ui->tabWidget->setTabText(ind, fn + " *");
    }
    else if (!isModified && captionMarked)
    {
        ui->tabWidget->setTabText(ind, fn);
    }
}

void MainWindow::on_actionOpen_triggered()
{
    _fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    _fileDialog.setFileMode(QFileDialog::ExistingFile);
    _fileDialog.setWindowTitle(tr("Open script"));
    _fileDialog.setNameFilters(QStringList() << tr("Script files (*.sql)") << tr("All files (*.*)"));
    _fileDialog.setHistory(_mruDirs);
    _fileDialog.fillEncodings();
    if (!_fileDialog.exec())
        return;
    QString fn = _fileDialog.selectedFiles().at(0);
    adjustMru();
    openFile(fn, _fileDialog.encoding());
}

QueryWidget *MainWindow::currentQueryWidget()
{
    if (!ui->tabWidget->count())
        return nullptr;
    QWidget *w = ui->tabWidget->widget(ui->tabWidget->currentIndex());
    return qobject_cast<QueryWidget*>(w);
}

void MainWindow::on_actionSave_triggered()
{
    ensureSaved(ui->tabWidget->currentIndex());
}

bool MainWindow::ensureSaved(int index, bool ask_name, bool forceWarning)
{
    QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(index));
    if (index >= ui->tabWidget->count() || index < 0 || !w)
        return true;

    if (w->isModified() || ask_name)
    {
        QString fn = ui->tabWidget->tabToolTip(index);
        if (fn.isEmpty())
            ask_name = true;
        if (/*!ask_name || */forceWarning)
        {
            QMessageBox::StandardButton answer_btn =
                    QMessageBox::warning(
                        this,
                        tr("Warning"),
                        tr("Save %1?").arg(fn),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
            if (answer_btn == QMessageBox::No)
                return true;
            if (answer_btn == QMessageBox::Cancel)
                return false;
            ask_name = fn.isEmpty();
        }
        QString encoding = w->encoding();
        if (ask_name)
        {
            if (fn.isEmpty() || ask_name)
            {
                _fileDialog.setAcceptMode(QFileDialog::AcceptSave);
                _fileDialog.setFileMode(QFileDialog::AnyFile);
                _fileDialog.setWindowTitle(tr("Save script"));
                _fileDialog.setNameFilters(QStringList() << tr("Script files (*.sql)") << tr("All files (*.*)"));
                _fileDialog.fillEncodings();
                _fileDialog.setEncoding(w->encoding());
                _fileDialog.setHistory(_mruDirs);
                if (!_fileDialog.exec())
                    return false;
                encoding = _fileDialog.encoding();
                fn = _fileDialog.selectedFiles().at(0);
                adjustMru();
            }
        }
        if (!w->saveFile(fn, encoding))
            return false;
        w->setModified(false);
        ui->tabWidget->setTabText(index, QFileInfo(fn).fileName());
        ui->tabWidget->setTabToolTip(index, fn);
    }
    return true;
}

QVariant MainWindow::current(const QString &nodeType, const QString &field)
{
    QModelIndex index = ui->objectsView->currentIndex();
    if (!index.isValid())
        return QVariant();
    DbObject *item = static_cast<DbObject*>(index.internalPointer());
    while (item)
    {
        if (item->data(DbObject::TypeRole).toString() == nodeType)
        {
            if (field.compare("name", Qt::CaseInsensitive))
                return item->data(DbObject::NameRole);
            if (field.compare("id", Qt::CaseInsensitive))
                return item->data(DbObject::IdRole);
            break;
        }
        else
            item = item->parent();
    }
    return QVariant();
}

QVariantList MainWindow::selected(const QString &nodeType, const QString &field)
{
    QVariantList res;
    QModelIndexList indexes = ui->objectsView->selectionModel()->selectedIndexes();
    for (const QModelIndex &i: indexes)
    {
        if (i.data(DbObject::TypeRole).toString() != nodeType)
            continue;
        if (field.compare("name", Qt::CaseInsensitive))
            res.append(i.data(DbObject::NameRole));
        else if (field.compare("id", Qt::CaseInsensitive))
            res.append(i.data(DbObject::IdRole));
    }
    return res;
}

void MainWindow::on_actionSave_as_triggered()
{
    ensureSaved(ui->tabWidget->currentIndex(), true);
}

void MainWindow::refreshActions()
{
    // TODO refactor QueryWidget/DbConnection mash

    QWidget *fw = QApplication::focusWidget();
    QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    ui->actionExecute_query->setEnabled(fw != ui->objectsView && ui->tabWidget->count() && w->dbConnection());
    QueryState qState = QueryState::Inactive;
    if (w && w->dbConnection())
        qState = w->isTimerActive() ? QueryState::Running : w->dbConnection()->queryState();

    ui->actionExecute_query->setIcon(qState == QueryState::Inactive ?
                                         QIcon(":img/control.png") :
                                         QIcon(":img/control-stop.png"));
    ui->actionExecute_query->setText(qState == QueryState::Inactive ?
                                         tr("Execute query") :
                                         tr("Stop execution"));
    if (qState != QueryState::Inactive)
        ui->actionExecute_query->setShortcuts(QList<QKeySequence>() << QKeySequence(Qt::CTRL + Qt::Key_F5));
    else
        ui->actionExecute_query->setShortcuts(QKeySequence::Refresh);

    ui->actionRefresh->setEnabled(fw == ui->objectsView);
    ui->actionChange_sort_mode->setEnabled(fw == ui->objectsView);

    QueryWidget *qw = (ui->tabWidget->isHidden() ? _objectScript : w);
    for (QAction *action: ui->menuEdit->actions())
        action->setEnabled(qw);
    _frPanel->setEditor(qw);
}

void MainWindow::adjustMru()
{
    // erase current dir if exists
    _mruDirs.removeAll(_fileDialog.directory().absolutePath());
    // put current dir on top
    _mruDirs.append(_fileDialog.directory().absolutePath());
    // Keep last 10 mru paths. Lets keep it const :)
    while (_mruDirs.size() > 10)
        _mruDirs.removeAt(0);
    _fileDialog.setHistory(_mruDirs);
    addMruFile();
}

void MainWindow::addMruFile()
{
    QString file = _fileDialog.selectedFiles().at(0);
    QString encoding = _fileDialog.encoding();
    auto actions = ui->menuOpen_recent->actions();

    QList<RecentFile> itemsToSave {{file, encoding}};
    QAction *a = new QAction(file, ui->menuOpen_recent);
    a->setData(encoding);
    connect(a, &QAction::triggered, this, &MainWindow::onActionOpenFile);
    ui->menuOpen_recent->insertAction(actions.isEmpty() ? nullptr : actions.first(), a);

    for (int i = 1; i < ui->menuOpen_recent->actions().size(); ++i)
    {
        QAction *a = ui->menuOpen_recent->actions()[i];
        if (!a->text().compare(file) || itemsToSave.size() == 15)
        {
            ui->menuOpen_recent->removeAction(a);
            delete a;
            --i;
            continue;
        }
        itemsToSave.append({a->text(), a->data().toString()});
    }

    SqtSettings::setValue("recentFiles", QVariant::fromValue(itemsToSave));
}

void MainWindow::scriptSelectedObjects()
{
    //if (!_objectScript->isVisible() && !ui->tableView->isVisible())
    //    return;
    _tableModel->clear();
    QModelIndex srcIndex =
            static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->
            mapToSource(ui->objectsView->currentIndex());
    if (!srcIndex.isValid())
    {
        _objectScript->clear();
        ui->tableView->hide();
        return;
    }
    QItemSelectionModel *selectionModel = qobject_cast<QItemSelectionModel*>(ui->objectsView->selectionModel());
    QModelIndexList si = selectionModel->selectedIndexes();

    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(srcIndex);
    if  (
            !con ||
            // do not try to open closed top-level connection
            (!con->isOpened() && srcIndex.data(DbObject::TypeRole).toString() == "connection") ||
            // open if not opened (shoud be database-level connection - it is closed when the node is collapsed)
            !con->open()
        )
    {
        showContent(srcIndex, nullptr);
        return;
    }
    con->clearResultsets();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    try
    {
        // process parent node in case of multiple selection, else process selected node
        QModelIndex parent = (si.count() > 1 ? srcIndex.parent() : srcIndex);
        QString type = parent.data(DbObject::TypeRole).toString();

        // callback to provide values for macroses
        auto env = [this, &parent, &si](QString macro) -> QVariant
        {
            // do we need children.names?!
            if (macro == "children.ids" || macro == "children.names")
            {
                DbObject::ObjectRole role = (macro == "children.ids" ?
                                                 DbObject::IdRole :
                                                 DbObject::NameRole);
                QString children;
                for(const QModelIndex &i: si)
                {
                    if (i.data(role).isValid())
                        children += (children.length() > 0 ? "," : "") +
                                i.data(role).toString();
                }
                return children.isEmpty() || si.count() == 1 ?
                            (role == DbObject::IdRole ? "-1" : "NULL") :
                            children;
            }

            return _objectsModel->parentNodeProperty(parent, macro);
        };

        if (si.count() > 1) // multiple selection - process parent node
        {
            // always refresh content in case of multiple selection
            auto c = Scripting::execute(con, Scripting::Context::Content, type, env);

            QModelIndex tmp_index;
            showContent(tmp_index, c.get());
        }
        else // single selection - process selected node
        {
            // check if script is not fetched yet
            std::unique_ptr<Scripting::CppConductor> c;
            if (!parent.data(DbObject::ContentRole).isValid())
            {
                c = Scripting::execute(con, Scripting::Context::Content, type, env);
                // special processing of 'connection' node: display dbmsInfo
                // if corresponding script is not found
                if (!c && type == "connection")
                {
                    QString dbmsInfo = con->dbmsInfo();
                    c = std::unique_ptr<Scripting::CppConductor>(new Scripting::CppConductor(con, env));
                    c->texts.append(dbmsInfo);
                }
                showContent(srcIndex, c.get());
            }
            else
                showContent(srcIndex, nullptr);

            // we can show preview if there is no resultset returned from previous script already
            if (!c || c->resultsets.empty())
            {
                // show preview if corresponding script exists
                auto c = Scripting::execute(con, Scripting::Context::Preview, type,
                                            [this, &srcIndex](QString macro) -> QVariant
                    {
                        return _objectsModel->parentNodeProperty(srcIndex, macro);
                    });

                DataTable *table = (c && !c->resultsets.isEmpty() ? c->resultsets.back() : nullptr);
                if (table)
                {
                    _tableModel->take(table);
                    ui->tableView->show();
                    ui->tableView->resizeColumnsToContents();
                }
                else
                    ui->tableView->hide();
            }
        }
    }
    catch (const QString &err)
    {
        onError(err);
    }
    catch (const std::runtime_error &e)
    {
        onError(QString::fromStdString(e.what()));
    }
}

void MainWindow::showContent(QModelIndex &index, const Scripting::CppConductor *content)
{
    /*
     * Current implementation displays only single item returned by script providing node's content.
     * We use first non-empty thing in the following order:
     * sql script -> html -> last resultset
     */

    _objectScript->clear();
    _tableModel->clear();

    QVariant value;
    QVariant type;
    // use new result data if available
    if (content)
    {
        if (!content->scripts.isEmpty())
        {
            value = content->scripts.back();
            type = "script";
        }
        else if (!content->htmls.isEmpty())
        {
            value = content->htmls.back();
            type = "html";
        }
        else if (!content->texts.isEmpty())
        {
            value = content->texts.back();
            type = "text";
        }

        if (index.isValid())
        {
            _objectsModel->setData(index, value, DbObject::ContentRole);
            _objectsModel->setData(index, type, DbObject::ContentTypeRole);
        }
    }
    // use preserved content data
    else if (index.isValid())
    {
        value = index.data(DbObject::ContentRole);
        type = index.data(DbObject::ContentTypeRole);
    }

    // if textual data exists - show it and stop processing
    if (value.isValid())
    {
        ui->tableView->hide();
        _objectScript->show();
        showTextualContent(value, type, content ? content->connection() : nullptr);
        return;
    }
    _objectScript->hide();

    // stop if no table to display
    if (!content || content->resultsets.isEmpty())
    {
        ui->tableView->hide();
        return;
    }

    ui->tableView->show();
    DataTable *table = content->resultsets.back();
    _tableModel->take(table);
    ui->tableView->resizeColumnsToContents();
    if (index.isValid())
        _objectsModel->setData(index, "table", DbObject::ContentTypeRole);
}

void MainWindow::showTextualContent(const QVariant &value, const QVariant &type, std::shared_ptr<DbConnection> con)
{
    if (!value.isValid())
        return;

    // for standalone usage
    _objectScript->show();

    if (type.toString() == "script")
    {
        _objectScript->setPlainText(value.toString());
        _objectScript->highlight(con);
    }
    else if (type.toString() == "html")
    {
        _objectScript->dehighlight();
        _objectScript->setHtml(value.toString());
    }
    else // "text" or empty
    {
        _objectScript->dehighlight();
        _objectScript->setPlainText(value.toString());
    }
}

void MainWindow::refreshContextInfo()
{
    if (!ui)
        return;

    DbConnection *con = nullptr;
    if (QApplication::focusWidget() != ui->objectsView && ui->tabWidget->count())
    {
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
        if (w)
            con = w->dbConnection();
    }
    else
    {
        QModelIndex srcIndex =
                static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->
                mapToSource(ui->objectsView->currentIndex());
        if (srcIndex.isValid())
            con = _objectsModel->dbConnection(srcIndex).get();
    }

    _contextLabel.setText(con ? con->context() : "");
}

void MainWindow::refreshCursorInfo()
{
    QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(QApplication::focusWidget());
    if (ed)
    {
        QTextCursor c = ed->textCursor(); //w->queryEditor()->textCursor();
        _positionLabel.setText(
                    QString("%1:%2").arg(c.blockNumber() + 1).arg(c.columnNumber() + 1) +
                    (c.hasSelection() ? QString("(%1)").arg(abs(c.position() - c.anchor())) : "")
                    );
    }
    _positionLabel.setVisible(ed);
}

void MainWindow::refreshConnectionState()
{
    QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    DbConnection *con = (w ? w->dbConnection() : nullptr);
    if (ui->tabWidget->isHidden() || !con)
        _durationLabel.clear();
    else
    {
        QString cn_status = con->transactionStatus();
        _durationLabel.setText(con->elapsed() +
                               (cn_status.isEmpty() ?
                                    "" :
                                    " <font color='red'>" + cn_status + "</font>"));
    }
}

void MainWindow::objectsViewAdjustColumnWidth(const QModelIndex &)
{
    ui->objectsView->header()->setStretchLastSection(false);
    ui->objectsView->resizeColumnToContents(0);
    if (ui->objectsView->columnWidth(0) < ui->objectsView->width())
        ui->objectsView->header()->setStretchLastSection(true);
}

void MainWindow::on_actionFind_triggered()
{
    QueryWidget *q = (ui->tabWidget->isHidden() ?
                          _objectScript :
                          qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget()));
    if (q)
        q->ShowFindPanel(_frPanel);
}

void MainWindow::on_tabWidget_currentChanged(int index)
{
    Q_UNUSED(index)
    QueryWidget *q = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    _frPanel->setEditor(q);
    refreshConnectionState();
}

void MainWindow::onActionOpenFile()
{
    QAction *a = qobject_cast<QAction*>(sender());
    QString fileName = a->text();
    if (!QFile(fileName).exists())
    {
        onError(tr("file %1 not found").arg(fileName));
        ui->menuOpen_recent->removeAction(a);
        delete a;

        QList<RecentFile> itemsToSave;
        for (const QAction *a: ui->menuOpen_recent->actions())
            itemsToSave.append({a->text(), a->data().toString()});

        SqtSettings::setValue("recentFiles", QVariant::fromValue(itemsToSave));
        return;
    }
    openFile(fileName, a->data().toString());
}

void MainWindow::openFile(const QString &fileName, const QString &encoding)
{
    int tabs_count = ui->tabWidget->count();
    ui->actionNew->activate(QAction::Trigger);
    if (ui->tabWidget->count() != tabs_count)
    {
        QueryWidget *w = currentQueryWidget();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);
        if (w->openFile(fileName, encoding))
        {
            ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), QFileInfo(fileName).fileName());
            ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), fileName);
        }
        _fileDialog.selectFile(fileName);
        _fileDialog.setEncoding(encoding);
        addMruFile();
    }
}

void MainWindow::log(const QString &msg)
{
    ui->log->appendPlainText(QString("%1: %2")
            .arg(QTime::currentTime().toString(Qt::ISODateWithMs))
            .arg(msg.trimmed()));

    int blockCount = ui->log->document()->blockCount();
    // limit log to 1000 rows
    if (blockCount > 1000)
    {
        auto b = ui->log->document()->findBlockByNumber(blockCount - 1001);
        QTextCursor c = ui->log->textCursor();
        c.movePosition(QTextCursor::Start);
        c.setPosition(b.position() + b.length(), QTextCursor::KeepAnchor);
        c.removeSelectedText();
        c.insertText("...");
    }
    ui->log->moveCursor(QTextCursor::End);
    ui->log->ensureCursorVisible();
}

void MainWindow::onMessage(const QString &msg)
{
    if (msg.isEmpty())
        return;

    QTextCharFormat fmt = ui->log->currentCharFormat();
    fmt.setForeground(QBrush(Qt::black));
    ui->log->mergeCurrentCharFormat(fmt);
    log(msg);
}

void MainWindow::onError(const QString &err)
{
    // empty message is used when already processed beforehand
    // (to prevent further code execution)
    if (err.isEmpty())
        return;

    if (!ui->splitterV->sizes().at(1))
    {
        ui->splitterV->setSizes({1000, 1});
        // hide in 6 secs
        _hideTimer->start();
    }
    else if (_hideTimer->isActive())
        _hideTimer->start();

    QTextCharFormat fmt = ui->log->currentCharFormat();
    fmt.setForeground(QBrush(Qt::red));
    ui->log->mergeCurrentCharFormat(fmt);
    log(err);
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dlg(this);
    dlg.exec();
}
