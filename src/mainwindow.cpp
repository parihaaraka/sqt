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
#include <QTabBar>
#include <QMenu>
#include <QClipboard>
#include <QFileInfo>
#include "tablemodel.h"
#include "dbtreeitemdelegate.h"
#include "findandreplacepanel.h"
#include <memory>
#include "scripting.h"
#include "sqllexer.h"
#include "codeeditor.h"
#include <QScrollBar>
#include "settingsdialog.h"
#include "queryoptions.h"
#include "resourcelocator.h"
#include "filesearchpanel.h"
#include "misc.h"
#include "textcodec.h"
#include <QCryptographicHash>
#include <QDir>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QActionGroup>
#endif

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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qRegisterMetaTypeStreamOperators<QList<RecentFile>>("RecentFile");
#endif
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
    // The preview pane has no messages pane of its own (it never runs a query),
    // so everything it has to say goes to the log - unprefixed, as it is not a
    // tab.
    connect(_objectScript, &QueryWidget::message, this, &MainWindow::onMessage);
    connect(_objectScript, &QueryWidget::error, this, &MainWindow::onError);
    ui->contentSplitter->insertWidget(0, _objectScript);
    ui->objectsView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->objectsView->setItemDelegateForColumn(0, new DbTreeItemDelegate(this));
    ui->objectsView->setStyle(_proxyStyle);
    connect(ui->objectsView, &QTreeView::expanded, this, &MainWindow::objectsViewAdjustColumnWidth);
    connect(ui->objectsView, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
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
    connect(_objectsModel, &DbObjectsModel::connectionStateChanged, this, [this]() {
        ui->objectsView->viewport()->update();
    });

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
    connect(ui->splitterV, &QSplitter::splitterMoved, this, [this]() { _hideTimer->stop(); });
    connect(_hideTimer, &QTimer::timeout, this, [this](){ ui->splitterV->setSizes({1000, 0}); });

    ui->actionRefresh->setShortcuts(QKeySequence::Refresh);
    ui->actionQuit->setShortcuts(QKeySequence::Quit);
    ui->actionNew->setShortcuts(QKeySequence::New);
    ui->actionOpen->setShortcuts(QKeySequence::Open);
    ui->actionSave->setShortcuts(QKeySequence::Save);
    ui->actionSave_as->setShortcuts(QKeySequence::SaveAs);
    ui->actionFind->setShortcuts(QKeySequence::Find);

    // Ctrl+E opens an editor tab for whatever is current: the previewed file
    // when the focus is in the preview pane, the search's file when the focus is
    // in the search panel, the tree's object otherwise.
    QAction *editAction = new QAction(this);
    editAction->setShortcut({"Ctrl+E"});
    connect(editAction, &QAction::triggered, this, [this]() {
        // Anywhere inside the search panel, not just its tree: after Enter in
        // the text field the focus is still there, and Ctrl+E then clearly means
        // "open what I have found", not "open the tree's object".
        QWidget *fw = QApplication::focusWidget();
        if (_searchPanel && fw && (fw == _searchPanel || _searchPanel->isAncestorOf(fw)))
        {
            if (const auto hit = _searchPanel->currentHit())
                openFileHitInEditor(*hit);
            return;
        }
        // Reading the found place in the pane and pressing Ctrl+E means "let me
        // edit this" - the file on screen, at the line being read. The pane is
        // not part of the search panel (it is the object content pane, borrowed
        // for the preview), so without this the key opened the script of a tree
        // node on a tab nobody was looking at.
        if (_paneHit && fw && (fw == _objectScript || _objectScript->isAncestorOf(fw)))
        {
            openPaneFileInEditor();
            return;
        }
        on_actionNew_triggered();
    });
    addAction(editAction);

    _searchPanel = new FileSearchPanel(this);
    ui->searchPage->layout()->addWidget(_searchPanel);
    // The panel's own remarks and failures follow the house rules: the log for
    // a failure, the status bar for a remark. Neither may reach a query's pane.
    connect(_searchPanel, &FileSearchPanel::message, this, &MainWindow::onMessage);
    connect(_searchPanel, &FileSearchPanel::error, this, &MainWindow::onError);
    connect(_searchPanel, &FileSearchPanel::statusMessage, this,
            [this](const QString &msg, int msecs) { ui->statusBar->showMessage(msg, msecs); });
    connect(_searchPanel, &FileSearchPanel::hitSelected, this,
            [this](FileSearchHit hit) { previewFileHit(hit, false); });
    connect(_searchPanel, &FileSearchPanel::hitActivated, this,
            [this](FileSearchHit hit) { previewFileHit(hit, true); });
    connect(_searchPanel, &FileSearchPanel::openInEditorRequested,
            this, &MainWindow::openFileHitInEditor);
    // An edited tab is what the user sees, so it is what gets searched - the
    // file on disk would give hits at the wrong lines and miss the new ones.
    _searchPanel->setBufferProvider([this]() {
        QHash<QString, QString> buffers;
        for (int i = 0; i < ui->tabWidget->count(); ++i)
        {
            QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i));
            if (w && w->isModified() && !w->fileName().isEmpty())
                buffers.insert(QFileInfo(w->fileName()).absoluteFilePath(), w->toPlainText());
        }
        return buffers;
    });

    // The content pane shows what the left pane's current tab points at, so
    // switching tabs takes the pane along: the results of a file search next to
    // the script of a database object nobody has selected read as one thing.
    // Only while that pane is the visible one, though - Ctrl+Shift+F pressed in
    // an editor tab must leave the editor on screen until a hit is chosen.
    connect(ui->objectsTab, &QTabWidget::currentChanged, this, [this](int) {
        if (ui->contentSplitter->isVisible())
            refreshContentPane();
    });

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
    const QList<RecentFile> fList = SqtSettings::value("recentFiles").value<QList<RecentFile>>();
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
    connect(tabWidgetAction, &QAction::triggered, this, [this]()
    {
        if (ui->tabWidget->currentIndex() < ui->tabWidget->count() - 1)
            ui->tabWidget->setCurrentIndex(ui->tabWidget->currentIndex() + 1);
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);

    // switch to previous tab page
    tabWidgetAction = new QAction(tr("previous page"), ui->tabWidget);
    tabWidgetAction->setShortcut(QKeySequence::Back);
    connect(tabWidgetAction, &QAction::triggered, this, [this]()
    {
        if (ui->tabWidget->currentIndex() > 0)
            ui->tabWidget->setCurrentIndex(ui->tabWidget->currentIndex() - 1);
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);

    // close tab page
    tabWidgetAction = new QAction(tr("close page"), ui->tabWidget);
    tabWidgetAction->setShortcuts(QKeySequence::Close);
    connect(tabWidgetAction, &QAction::triggered, this, [this]()
    {
        int index = targetTabIndex();
        if (index >= 0)
            closeTab(index);
        QWidget *w = ui->tabWidget->currentWidget();
        if (w)
            w->setFocus();
    });
    ui->tabWidget->tabBar()->addAction(tabWidgetAction);

    // file-related actions of the tab bar context menu
    // (hidden along with the separator unless the target tab has a file)
    QAction *tabFilesSeparator = new QAction(ui->tabWidget);
    tabFilesSeparator->setSeparator(true);
    ui->tabWidget->tabBar()->addAction(tabFilesSeparator);
    _fileTabActions.append(tabFilesSeparator);

    // every such action just copies some part of the file name to the clipboard
    auto addFileNameAction = [this](const QString &title, QString (QFileInfo::*extract)() const)
    {
        QAction *action = new QAction(title, ui->tabWidget);
        connect(action, &QAction::triggered, this, [this, extract]()
        {
            QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(targetTabIndex()));
            if (!w || w->fileName().isEmpty())
                return;
            QFileInfo fi(w->fileName());
            QApplication::clipboard()->setText((fi.*extract)());
        });
        ui->tabWidget->tabBar()->addAction(action);
        _fileTabActions.append(action);
    };
    addFileNameAction(tr("copy file name"), &QFileInfo::fileName);
    addFileNameAction(tr("copy absolute path"), &QFileInfo::absoluteFilePath);

    // Take the context menu over to let the actions know the tab under cursor.
    // QMenu::exec() is modal, so an action is triggered within it - the recorded
    // index is valid exactly while the menu is up.
    QTabBar *tabBar = ui->tabWidget->tabBar();
    tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar, &QWidget::customContextMenuRequested, this, [this, tabBar](const QPoint &pos)
    {
        int index = tabBar->tabAt(pos);
        if (index < 0) // beyond the tabs
            return;
        _menuTabIndex = index;
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(index));
        bool hasFileName = (w && !w->fileName().isEmpty());
        for (QAction *action: std::as_const(_fileTabActions))
            action->setVisible(hasFileName);
        QMenu::exec(tabBar->actions(), tabBar->mapToGlobal(pos));
        _menuTabIndex = -1;
    });
    ui->tabWidget->setMovable(true);

    // corner buttons
    QWidget *cornerWidget = new QWidget();
    QHBoxLayout *l = new QHBoxLayout();
    l->setContentsMargins(2, 0, 2, 4);
    QToolButton *dbBtn = new QToolButton(ui->tabWidget);
    dbBtn->setAutoRaise(true);
    QMenu *dbBtnMenu = new QMenu(dbBtn);
    connect(dbBtnMenu, &QMenu::aboutToShow, this, [this, dbBtnMenu](){
        dbBtnMenu->clear();
        QueryWidget *qw = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
        DbConnection *currentConnection = (qw ? qw->dbConnection() : nullptr);
        auto m = ui->objectsView->model();
        for (int cr = 0; cr < m->rowCount(); ++cr) // iterate connections
        {
            QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(m)->mapToSource(m->index(cr, 0));
            DbConnection *connection = _objectsModel->dbConnection(srcIndex).get();
            // A connection object exists from the first successful connect until
            // an explicit Disconnect, and that is exactly the lifetime of these
            // menu items. Its link may well be broken at the moment - the tree
            // keeps such a server with a red indicator - but picking a database
            // opens it again, so there is no reason to hide anything here.
            if (connection)
            {
                // collect databases on next level only
                // (if somebody build folders of databases, then databases will not be found)
                QStringList databases;
                for (int dr = 0; dr < m->rowCount(m->index(cr, 0)); ++dr)
                {
                    QModelIndex ind = m->index(dr, 0, m->index(cr, 0));
                    if (ind.data(DbObject::TypeRole).toString() != "database")
                        continue;
                    QModelIndex srcIndex2 = static_cast<QSortFilterProxyModel*>(m)->mapToSource(ind);
                    // a database node keeps its own connection, created when the
                    // node appears; nothing guarantees it is still registered
                    if (auto dbCon = _objectsModel->dbConnection(srcIndex2))
                        databases.append(dbCon->database());
                }

                if (databases.isEmpty())
                {
                    dbBtnMenu->addAction(srcIndex.data().toString(), this, [this, qw, connection](){
                        DbConnection *cn = connection->clone();
                        qw->setDbConnection(cn);
                        retitleOnDatabaseChange(qw);
                        refreshContextInfo();
                    });
                }
                else if (currentConnection && connection->connectionString() == currentConnection->connectionString())
                {
                    // put databases of current connection on top level
                    auto actionsList = dbBtnMenu->actions();
                    QAction *next = actionsList.isEmpty() ? nullptr : actionsList.first();
                    QList<QAction*> actions;
                    for (int i = 0; i < databases.size(); ++i)
                    {
                        if (currentConnection->database() != databases[i])
                        {
                            QAction *a = new QAction(databases[i], dbBtnMenu);
                            connect(a, &QAction::triggered, this, [this, a, qw, currentConnection](){
                                currentConnection->setDatabase(a->text());
                                currentConnection->open();
                                retitleOnDatabaseChange(qw);
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
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                    for (const QString &db: qAsConst(databases))
#else
                    for (const QString &db: std::as_const(databases))
#endif
                        menu->addAction(db, this, [this, qw, connection, db](){
                            DbConnection *cn = connection->clone();
                            cn->setDatabase(db);
                            qw->setDbConnection(cn);
                            retitleOnDatabaseChange(qw);
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
                       tr("sql query tool v%1<br/>&copy; 2013-2026 Andrey Lukyanov<br/>freeware<br/><br/>"
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
        // the state indicator is drawn from the connection itself, so a link
        // lost behind the scenes leaves it stale until something repaints
        connect(con.get(), &DbConnection::connectionLost, this, [this]() {
            ui->objectsView->viewport()->update();
        });
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
        // "Disconnect" is what clears the tree and the connections menu, so it
        // must stay available for a registered connection whose link has died
        actionConnect = myMenu.addAction(con ? tr("Disconnect") : tr("Connect"));
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
                        _objectsModel->data(srcIndex, Qt::EditRole).toString(),
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
        bool wasRegistered = false;
        if (con) // "disconnect" in any case (clear tree)
        {
            wasRegistered = true;
            // there are problems with expanding desolated node without the following line
            ui->objectsView->collapse(index);

            DbObject *item = static_cast<DbObject*>(srcIndex.internalPointer());
            _objectsModel->removeRows(0, item->childCount(), srcIndex);
            con->close();
            con->disconnect(); // disconnect all slots from all signals
            DbConnectionFactory::removeConnection(QString::number(std::intptr_t(item)));
            _objectsModel->setData(srcIndex, false, DbObject::ParentRole);
            _objectsModel->setData(srcIndex, QVariant(), DbObject::ContentRole);
            _objectsModel->setData(srcIndex, QVariant(), DbObject::ChildObjectsCountRole);
            showContent(srcIndex, nullptr);
        }
        if (!wasRegistered)
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
        // hl.conf may have been edited as well
        SqlLexer::clearCache();
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
    emit _objectsModel->dataChanged(nodeToRefresh, nodeToRefresh);

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
    const QModelIndexList si = selectionModel->selectedIndexes();
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
        refreshContentPane();
    else if (ui->tabWidget->currentWidget())
        ui->tabWidget->currentWidget()->setFocus();
}

void MainWindow::on_actionExecute_query_triggered()
{
    QueryWidget *q = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    DbConnection *con = q->dbConnection();
    if (!con)
        return;
    auto qState = con->queryState();
    if (qState == QueryState::Running || qState == QueryState::Reconnecting ||
        qState == QueryState::Cancelling || q->isTimerActive())
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
        executeQuery(q, false);
}

void MainWindow::executeQuery(QueryWidget *q, bool currentStatementOnly)
{
    DbConnection *con = q->dbConnection();
    if (!con || con->queryState() != QueryState::Inactive || q->isTimerActive())
        return;

    q->clearResult();
    QString query;
    // What the empty case is about, so that it can be said out loud below.
    // Which of the three applies is decided here and nowhere else.
    QString nothingToRun;
    QTextCursor cursor = q->textCursor();
    if (cursor.hasSelection())
    {
        query = cursor.selection().toPlainText();
        nothingToRun = tr("the selection holds no statement to run");
    }
    else if (currentStatementOnly)
    {
        // Falls back to the whole text where no dictionary-driven lexer is
        // available for this connection (currentStatementBounds() returns
        // {-1, -1}, e.g. no hl.conf shipped for this dbms yet) - same
        // behaviour Ctrl+Return would otherwise have without this feature.
        const QPair<int, int> bounds = q->currentStatementBounds();
        query = q->toPlainText();
        if (bounds.first >= 0)
        {
            query = query.mid(bounds.first, bounds.second - bounds.first);
            // Between two statements, or past the last separator: there is text
            // in the tab, just none of it at the caret.
            nothingToRun = tr("no statement at the caret");
        }
        else
            nothingToRun = tr("the tab is empty");
    }
    else
    {
        query = q->toPlainText();
        nothingToRun = tr("the tab is empty");
    }

    // Whitespace and nothing else is not worth sending, and libpq answers an
    // empty query with an empty result - which looks exactly like the
    // application having ignored the key. Said in the messages pane: the user
    // asked for a run, and this is that run's outcome.
    if (query.trimmed().isEmpty())
    {
        q->note(nothingToRun);
        return;
    }

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
    // through the widget, so that the connection's output is recognized as
    // this query's result and lands in the tab's messages pane
    q->execute(query);
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
    // A server that has never been connected has no connection object, and
    // opening one here would bypass the login dialog; a registered one is
    // reopened by open() itself, broken link or not.
    if (!con || !con->open())
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
    // a manually created tab has no name of its own to show
    w->setTitle(autoTabTitle(w), true);
    updateTabCaption(w);

    if (sender() != ui->actionNew && _objectsModel->data(srcIndex, DbObject::ContentTypeRole).toString() == "script")
        w->setPlainText(_objectsModel->data(srcIndex, DbObject::ContentRole).toString());
    else
        w->setPlainText("");

    w->highlight();
    connect(w, &QueryWidget::sqlChanged, this, &MainWindow::sqlChanged);
    // anything not produced by a query run belongs to the log, not to the tab
    connect(w, &QueryWidget::message, this, &MainWindow::onMessage);
    connect(w, &QueryWidget::error, this, &MainWindow::onError);
    w->setReadOnly(false);

    if (ui->contentSplitter->isVisible())
        ui->actionQuery_editor->activate(QAction::Trigger);
    w->setFocus();

    // The tab got a clone of its own above, so the link opened here for the
    // sake of cloning is of no further use to a node that stays collapsed.
    releaseIdleDatabaseConnection(srcIndex);
}

void MainWindow::on_tabWidget_tabCloseRequested(int index)
{
    closeTab(index);
}

// The only place a tab caption is composed. Deriving it from the file name here
// used to wipe the captions of the tabs having no file behind them (created
// manually or by F4) as soon as they were edited.
void MainWindow::updateTabCaption(QueryWidget *w)
{
    int ind = ui->tabWidget->indexOf(w);
    if (ind == -1)
        return;
    ui->tabWidget->setTabText(ind, w->title() + (w->isModified() ? " *" : ""));
}

// A name for a tab having neither a file nor an object behind it. The database
// tells the tabs apart at a glance, the number keeps the caption unique.
QString MainWindow::autoTabTitle(const QueryWidget *w) const
{
    DbConnection *cn = const_cast<QueryWidget*>(w)->dbConnection();
    QString db = (cn ? cn->database() : QString());
    if (db.isEmpty())
        db = tr("query");

    for (int n = 1; ; ++n)
    {
        QString candidate = db + ' ' + QString::number(n);
        bool taken = false;
        for (int i = 0; i < ui->tabWidget->count() && !taken; ++i)
        {
            QueryWidget *other = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i));
            taken = (other && other != w && other->title() == candidate);
        }
        if (!taken)
            return candidate;
    }
}

// An autogenerated caption names the database, so it must follow a switch to
// another one. A file or object name is the tab's own and is left alone.
void MainWindow::retitleOnDatabaseChange(QueryWidget *w)
{
    if (!w || !w->titleIsAuto())
        return;
    w->setTitle(autoTabTitle(w), true);
    updateTabCaption(w);
}

void MainWindow::releaseIdleDatabaseConnection(const QModelIndex &srcIndex)
{
    if (!srcIndex.isValid())
        return;

    // The owner of the link is the nearest "database" ancestor, the same one
    // dbConnection() would settle on. A "connection" node found on the way up
    // means the branch belongs to a server, not to a database: those links are
    // the user's business (the Connect/Disconnect menu), so nothing is closed.
    DbObject *owner = static_cast<DbObject*>(srcIndex.internalPointer());
    while (owner && owner->data(DbObject::TypeRole).toString() != "database")
    {
        if (owner->data(DbObject::TypeRole).toString() == "connection")
            return;
        owner = owner->parent();
    }
    if (!owner)
        return;

    auto con = DbConnectionFactory::connection(QString::number(std::intptr_t(owner)));
    if (!con || !con->isOpened())
        return;

    // The node's own index, needed both to ask the view about its state and to
    // repaint the indicator afterwards.
    QModelIndex ownerSrcIndex = srcIndex;
    while (ownerSrcIndex.isValid() &&
           static_cast<DbObject*>(ownerSrcIndex.internalPointer()) != owner)
        ownerSrcIndex = ownerSrcIndex.parent();
    if (!ownerSrcIndex.isValid())
        return;

    auto proxy = static_cast<QSortFilterProxyModel*>(ui->objectsView->model());
    const QModelIndex ownerIndex = proxy->mapFromSource(ownerSrcIndex);

    // An expanded branch is exactly what earns a link the right to stay: its
    // children are on screen and the next click on any of them will need it.
    if (ui->objectsView->isExpanded(ownerIndex))
        return;

    // Never pull the rug from under a query, and never discard a transaction
    // the user has opened on this very connection.
    if (con->queryState() != QueryState::Inactive || !con->transactionStatus().isEmpty())
        return;

    // An editor tab holds a clone of its own, so no tab is affected by this.
    // The preview pane is lent the tree's connection to build its highlighter
    // dictionary from, and keeps a shared_ptr to it - which is harmless: the
    // dictionary is already built, the pane never runs a query, and the object
    // stays registered anyway, only its link is gone.
    con->close();

    // the indicator is painted from the connection itself, so the row has to
    // be repainted for it to turn red
    ui->objectsView->viewport()->update();
    // the status bar shows this connection's context - now an empty one
    refreshContextInfo();
}

void MainWindow::sqlChanged()
{
    updateTabCaption(qobject_cast<QueryWidget*>(sender()));
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

QueryWidget *MainWindow::openScriptTab(const QString &text, const QString &title, DbConnection *connection)
{
    // An untouched tab holding this very script is the tab the user is asking
    // for, so reuse it instead of stacking up duplicates. Comparing the text
    // rather than the object name covers both a redefined object and the same
    // name in another database: either way the script differs and deserves a
    // tab of its own.
    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        QueryWidget *existing = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i));
        if (existing && existing->isGeneratedScript() &&
            existing->toPlainText().trimmed() == text.trimmed())
        {
            ui->tabWidget->setCurrentIndex(i);
            if (ui->contentSplitter->isVisible())
                ui->actionQuery_editor->activate(QAction::Trigger);
            existing->setFocus();
            return existing;
        }
    }

    QueryWidget *w = (connection ?
                          new QueryWidget(connection, ui->tabWidget) :
                          new QueryWidget(ui->tabWidget));
    int ind = ui->tabWidget->addTab(w, title);
    ui->tabWidget->setCurrentIndex(ind);
    // the object name is the tab's own, so it must survive editing
    w->setTitle(title);
    w->setGeneratedScript(true);
    w->setPlainText(text);
    w->highlight();
    connect(w, &QueryWidget::sqlChanged, this, &MainWindow::sqlChanged);
    // anything not produced by a query run belongs to the log, not to the tab
    connect(w, &QueryWidget::message, this, &MainWindow::onMessage);
    connect(w, &QueryWidget::error, this, &MainWindow::onError);
    w->setReadOnly(false);
    w->setModified(false);

    if (ui->contentSplitter->isVisible())
        ui->actionQuery_editor->activate(QAction::Trigger);
    w->setFocus();
    return w;
}

QueryWidget *MainWindow::currentQueryWidget()
{
    if (!ui->tabWidget->count())
        return nullptr;
    QWidget *w = ui->tabWidget->widget(ui->tabWidget->currentIndex());
    return qobject_cast<QueryWidget*>(w);
}

// The tab a tab bar action must be applied to: the one under cursor when
// invoked through the context menu, the current one when invoked by shortcut.
int MainWindow::targetTabIndex() const
{
    return _menuTabIndex >= 0 ? _menuTabIndex : ui->tabWidget->currentIndex();
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
        w->setTitle(QFileInfo(fn).fileName());
        updateTabCaption(w);
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
    const QModelIndexList indexes = ui->objectsView->selectionModel()->selectedIndexes();
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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
        ui->actionExecute_query->setShortcuts(QList<QKeySequence>() << QKeySequence(Qt::CTRL + Qt::Key_F5));
#else
        ui->actionExecute_query->setShortcuts(QList<QKeySequence>() << QKeySequence(Qt::CTRL | Qt::Key_F5));
#endif
    else
        ui->actionExecute_query->setShortcuts(QKeySequence::Refresh);

    ui->actionRefresh->setEnabled(fw == ui->objectsView);
    ui->actionChange_sort_mode->setEnabled(fw == ui->objectsView);

    QueryWidget *qw = (ui->tabWidget->isHidden() ? _objectScript : w);
    const auto actions = ui->menuEdit->actions();
    for (QAction *action: actions)
    {
        // Find/replace needs an editor to work on, but the two navigation
        // entries do not: switching to the file search (or back to the tree) is
        // exactly what one wants with no tab open, and disabling an action
        // disables its shortcut with it.
        if (action == ui->actionFind_in_files || action == ui->actionObject_tree)
            continue;
        action->setEnabled(qw);
    }
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
        QAction *a2 = ui->menuOpen_recent->actions().at(i);
        if (!a2->text().compare(file) || itemsToSave.size() == 15)
        {
            ui->menuOpen_recent->removeAction(a2);
            delete a2;
            --i;
            continue;
        }
        itemsToSave.append({a2->text(), a2->data().toString()});
    }

    SqtSettings::setValue("recentFiles", QVariant::fromValue(itemsToSave));
}

void MainWindow::scriptSelectedObjects()
{
    //if (!_objectScript->isVisible() && !ui->tableView->isVisible())
    //    return;
    // From here on the pane belongs to a tree node, not to a file: Ctrl+E in it
    // means "the node's script" again, and refreshContentPane() has nothing to
    // rebuild.
    _paneHit.reset();
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
    const QModelIndexList si = selectionModel->selectedIndexes();

    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(srcIndex);
    // same as in on_actionNew_triggered(): no object means never connected,
    // while a broken link is restored by open() without bothering the user
    if (!con || !con->open())
    {
        showContent(srcIndex, nullptr);
        return;
    }
    con->clearResultsets();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    try
    {
        // Process parent node in case of multiple selection, else process
        // selected node. A node collecting children of a single kind may have
        // no content script of its own (the "Columns" node a wide table hides
        // its columns behind), so the nearest ancestor that has one is scripted
        // instead - with $children.ids$ still naming the selected nodes. This
        // keeps such a folder free of a script duplicating the owner's one.
        QModelIndex parent = srcIndex;
        if (si.count() > 1)
        {
            parent = srcIndex.parent();
            for (QModelIndex i = parent; i.isValid(); i = i.parent())
            {
                if (Scripting::getScript(con.get(), Scripting::Context::Content,
                                         i.data(DbObject::TypeRole).toString()))
                {
                    parent = i;
                    break;
                }
            }
        }
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
                auto cnd = Scripting::execute(con, Scripting::Context::Preview, type,
                                              [this, &srcIndex](QString macro) -> QVariant
                {
                    return _objectsModel->parentNodeProperty(srcIndex, macro);
                });

                DataTable *table = (cnd && !cnd->resultsets.isEmpty() ? cnd->resultsets.back() : nullptr);
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

    // The content and preview scripts above are the only thing a mere selection
    // needs the link for. Everything is fetched by now (the panes hold copies),
    // so a database whose node is still collapsed gets its link back instead of
    // keeping a backend busy until the end of the session.
    releaseIdleDatabaseConnection(srcIndex);
}

void MainWindow::showContent(QModelIndex &index, const Scripting::CppConductor *content)
{
    /*
     * Current implementation displays only single item returned by script providing node's content.
     * We use first non-empty thing in the following order:
     * sql script -> html -> last resultset
     */

    // Whatever the pane held, it is a tree node's content from here on. Not only
    // scriptSelectedObjects() ends up here - Disconnect clears the pane through
    // this function directly - and a stale file left remembered would send
    // Ctrl+E to a file nobody is looking at.
    _paneHit.reset();

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
        // Cached content comes with no conductor, but the pane still has to be
        // told which node it is showing: highlight() keeps its previous
        // connection when handed a nullptr, and would then dictionary itself
        // from a node the user has long left (in the worst case reopening its
        // link). The node's own connection is the right answer in both cases.
        showTextualContent(value, type,
                           content ? content->connection() :
                                     (index.isValid() ? _objectsModel->dbConnection(index) : nullptr));
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

    auto adjust_visualizeWhitespace = [this](bool sql) {
        if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_objectScript->editor()))
        {
            QTextOption textOption(plain->document()->defaultTextOption());
            QTextOption::Flags currentFlags = textOption.flags();
            if (SqtSettings::value("visualizeWhitespace", false).toBool() && sql)
                currentFlags |= QTextOption::ShowTabsAndSpaces;
            else
                currentFlags &= ~QTextOption::ShowTabsAndSpaces;
            textOption.setFlags(currentFlags);
            plain->document()->setDefaultTextOption(textOption);
        }
    };

    if (type.toString() == "script")
    {
        adjust_visualizeWhitespace(true);
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
        // don't visualize whitespaces in non-sql content
        adjust_visualizeWhitespace(false);
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
    else if (con->queryState() == QueryState::Reconnecting)
        _durationLabel.setText("<font color='red'>" + tr("reconnecting...") + "</font>");
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
        const auto actions = ui->menuOpen_recent->actions();
        for (const QAction *ar: actions)
            itemsToSave.append({ar->text(), ar->data().toString()});

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
            w->setTitle(QFileInfo(fileName).fileName());
            updateTabCaption(w);
            ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), fileName);
        }
        _fileDialog.selectFile(fileName);
        _fileDialog.setEncoding(encoding);
        addMruFile();
    }
}

void MainWindow::log(const QString &msg)
{
    ui->log->appendPlainText(QString("%1: %2").arg(QTime::currentTime().toString(Qt::ISODateWithMs), msg.trimmed()));

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

    const QPalette defaultPalette;
    QTextCharFormat fmt = ui->log->currentCharFormat();
    fmt.setForeground(QBrush(defaultPalette.color(QPalette::Text)));
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
    fmt.setForeground(QBrush(QColor::fromRgba(0xE0FF4040))); // NOLINT
    ui->log->mergeCurrentCharFormat(fmt);
    log(err);
}

void MainWindow::on_actionSettings_triggered()
{
    const QString assetsDir = SqtSettings::value("assetsDir").toString();
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted &&
        SqtSettings::value("assetsDir").toString() != assetsDir)
    {
        reloadAssets();
    }
}

void MainWindow::reloadAssets()
{
    setAppResourcesUserDir(SqtSettings::value("assetsDir").toString());

    // Everything read through the locator once and kept since. The scripts and
    // the dictionaries are reread on the next request; the icons are asked for
    // explicitly, being held as pixmaps rather than as names.
    Scripting::clearCache();
    SqlLexer::clearCache();
    _proxyStyle->loadIcons();
    _objectsModel->reloadIcons();
    ui->objectsView->viewport()->update();

    // The highlighter is built once per connection and holds its dictionary, so
    // it has to be rebuilt even though the connection has not changed.
    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        if (auto w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i)))
            w->highlight(nullptr, true);
    }
    if (_objectScript && _objectScript->dbConnection())
        _objectScript->highlight(nullptr, true);
}

QString MainWindow::searchProfileKey(const std::shared_ptr<DbConnection> &con, QString *label)
{
    if (label)
        label->clear();
    if (!con)
        return QString();

    // The connection string names the server; the database within it may be
    // switched at any time and the scripts still belong to the same repository,
    // so it is deliberately not part of the identity.
    QString cs = con->connectionString();
    if (cs.isEmpty())
        return QString();

    // The password never goes into the key material: the settings file is plain
    // text, and a digest of a secret is still something one should not store.
    static const QRegularExpression pwd(
                R"((^|\s)(password)\s*=\s*('(?:[^'\\]|\\.)*'|\S*))",
                QRegularExpression::CaseInsensitiveOption);
    cs.replace(pwd, "\\1");

    if (label)
    {
        // Readable, and enough to recognize the entry by: the live context when
        // there is one ("user@host:port/db"), the sanitized string otherwise.
        const QString ctx = con->context();
        *label = (ctx.isEmpty() ? cs.simplified() : ctx);
    }

    return QString::fromLatin1(
                QCryptographicHash::hash(cs.simplified().toUtf8(),
                                         QCryptographicHash::Sha1).toHex().left(16));
}

void MainWindow::on_actionObject_tree_triggered()
{
    // Ctrl+Shift+O is the way back from the search results to the tree. The
    // focus goes with it: the point is to keep working from the keyboard, and a
    // raised tab whose tree is not focused still needs a click.
    ui->objectsTab->setCurrentWidget(ui->objectsPage);
    ui->objectsView->setFocus();
}

void MainWindow::on_actionFind_in_files_triggered()
{
    // The connection of the context the shortcut came from. A script on disk is
    // written against a particular database, and the tab (or the tree node) the
    // user was looking at names it better than anything else we could guess.
    std::shared_ptr<DbConnection> con;
    if (QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
        w && !ui->tabWidget->isHidden())
    {
        con = w->sharedDbConnection();
    }
    if (!con)
    {
        QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->
                mapToSource(ui->objectsView->currentIndex());
        if (srcIndex.isValid())
            con = _objectsModel->dbConnection(srcIndex);
    }
    // A clone of its own, not the borrowed link: the original belongs to a tree
    // node or to a tab, either of which may be gone by the next search, and the
    // search would then have no dbms left to highlight or open a file with (see
    // the member's comment). Nothing is opened here - a clone knows its dbms
    // without a link - so this costs no backend.
    if (con)
    {
        _searchConnection.reset(con->clone());
        // Its own object, so its own reports; the log, since a file search has
        // no messages pane of its own and none of this is a query's result.
        connect(_searchConnection.get(), &DbConnection::error, this, &MainWindow::onError);
        connect(_searchConnection.get(), &DbConnection::message, this, &MainWindow::onMessage);
        con = _searchConnection;
        // Each connection has its own root folder, and switching to this one
        // brings its folder back. Done before the panel is shown, so that the
        // path field is already right when it appears.
        QString label;
        const QString key = searchProfileKey(con, &label);
        _searchPanel->setConnectionProfile(key, label);

        // The results are colored with the very dictionary the editor uses for
        // this dbms, so a match in the tree and the same text in the preview
        // read alike. A bundle without a palette of its own is not an error -
        // the panel falls back to the window's palette.
        try
        {
            QJsonDocument hlSettings;
            if (const QString hl = Scripting::dbmsFile(con.get(), "hl.conf"); !hl.isEmpty())
                hlSettings = readJsonFile(hl);
            _searchPanel->setHighlightSettings(hlSettings);
        }
        catch (const QString &err)
        {
            log(err);
        }
    }

    // The selected text, or the word under the cursor - the same seeding the
    // find panel does, and the usual reason one presses this shortcut.
    QString seed;
    if (QueryWidget *w = (ui->tabWidget->isHidden() ?
                              _objectScript :
                              qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget())))
    {
        QTextCursor c = w->textCursor();
        if (!c.hasSelection())
            c.select(QTextCursor::WordUnderCursor);
        // a multiline selection is a block of code, not something to look for
        seed = c.selectedText();
        if (seed.contains(QChar::ParagraphSeparator) || seed.contains('\n'))
            seed.clear();
    }

    ui->objectsTab->setCurrentWidget(ui->searchPage);
    _searchPanel->setSearchText(seed);
    _searchPanel->activateSearchField();
}

void MainWindow::gotoFilePosition(QueryWidget *w, int line, int column, int length,
                                  const QColor &matchColor)
{
    QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(w->editor());
    if (!ed)
        return;

    QTextBlock block = ed->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
    {
        // A stale line number: whatever the pane marked before is not the place
        // asked for, and leaving the old mark behind would be a lie.
        w->clearMatchHighlight();
        return;
    }

    QTextCursor c(block);
    // Column and length come from the same normalized text the search read, so
    // they are safe to use directly - but a file changed since is not, hence
    // the clamping to the block.
    const int col = qBound(0, column - 1, block.length() - 1);
    c.setPosition(block.position() + col);
    if (length > 0)
    {
        const int end = qMin(block.position() + col + length,
                             ed->document()->characterCount() - 1);
        c.setPosition(end, QTextCursor::KeepAnchor);
    }
    w->setTextCursor(c);
    // centerCursor() rather than ensureCursorVisible(): the point of the jump is
    // to see the match in its surroundings, not pinned to the last line.
    ed->centerCursor();

    // The selection alone is not enough to show where the match is. The pane
    // keeps no focus (the results tree has it, and that is the point - the
    // arrows keep walking the hits), so the selection is painted from the
    // palette's Inactive group, which in most themes is a grey barely different
    // from the background. The mark below is painted by the editor itself, in a
    // colour of our choosing, and does not care about the focus.
    if (c.hasSelection())
        w->setMatchHighlight(c, matchColor);
    else
        w->clearMatchHighlight();
}

void MainWindow::previewFileHit(const FileSearchHit &hit, bool focusPane)
{
    // The pane the object content is normally shown in. Switching the view mode
    // here would fight the user, so a hidden pane is simply raised - the search
    // results and the preview belong to the same glance.
    if (ui->contentSplitter->isHidden())
        ui->actionObject_content->activate(QAction::Trigger);

    // The buffer of a modified tab is what was searched, so it is what must be
    // previewed; the file on disk would show the match at the wrong line.
    QString text;
    bool fromBuffer = false;
    const QString absolute = QFileInfo(hit.fileName).absoluteFilePath();
    for (int i = 0; i < ui->tabWidget->count() && !fromBuffer; ++i)
    {
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i));
        if (w && w->isModified() && !w->fileName().isEmpty() &&
            QFileInfo(w->fileName()).absoluteFilePath() == absolute)
        {
            text = w->toPlainText();
            fromBuffer = true;
        }
    }

    if (!fromBuffer)
    {
        FileSearchParams params;
        // Decoded exactly as the search decoded it, so the preview shows the
        // text the hit's line and column were measured in.
        params.encoding = hit.encoding;
        params.fallbackEncoding = SqtSettings::value("encodings").toString()
                .split(',', Qt::SkipEmptyParts).value(1).trimmed();
        // Reading the whole file to show a fragment is what makes the sql
        // highlighting and the surrounding lines possible; these are scripts,
        // and the search has already refused anything of unreasonable size.
        FileSearch::FileText content = FileSearch::readFile(hit.fileName, params);
        if (!content.error.isEmpty())
        {
            // a failure the user did not ask for goes to the log, not to a popup
            onError(tr("`%1`: %2").arg(QDir::toNativeSeparators(hit.fileName), content.error));
            return;
        }
        text = content.text;
    }

    // Interpreted as sql and highlighted with the dictionary of the connection
    // that was current when the search was invoked.
    _tableModel->clear();
    ui->tableView->hide();
    showTextualContent(text, "script", _searchConnection);
    // The title bar has no room for this, so the file the pane is showing is
    // named in the status bar - it is not obvious from the text itself.
    ui->statusBar->showMessage(QString("%1:%2").arg(
                                   QDir::toNativeSeparators(hit.fileName)).arg(hit.line), 5000);

    // The tree's own match colour, so that the highlighted fragment in the
    // results and the marked place in the pane are visibly the same thing.
    gotoFilePosition(_objectScript, hit.line, hit.column, hit.length,
                     _searchPanel ? _searchPanel->matchColor() : QColor());
    if (focusPane)
        _objectScript->setFocus();

    // What the pane is showing now - Ctrl+E in it opens this very file, and a
    // switch back to the tree knows the pane has to be rebuilt.
    _paneHit = hit;
}

void MainWindow::refreshContentPane()
{
    // Which of the left pane's tabs is up decides what the pane shows. F2 used
    // to call scriptSelectedObjects() outright, so switching to the content view
    // while the search results were on screen put the script of a database
    // object nobody had selected there - and the found place, which is what the
    // key was pressed for, was nowhere to be seen.
    if (_searchPanel && ui->objectsTab->currentWidget() == ui->searchPage)
    {
        if (const auto hit = _searchPanel->currentHit())
            previewFileHit(*hit, false);
        // No results yet, or nothing selected among them: the pane is left as it
        // is. Clearing it would throw away a preview that is still worth reading,
        // and the tree's object has no business appearing here either.
        return;
    }
    // Back on the tree. The node's script is rebuilt only if a file has taken
    // the pane over in the meantime - rerunning the content script on every tab
    // switch would reopen the link that selecting the node has just released.
    if (!_paneHit)
        return;
    _paneHit.reset();
    scriptSelectedObjects();
}

void MainWindow::openFileHitInEditor(const FileSearchHit &hit)
{
    const QString absolute = QFileInfo(hit.fileName).absoluteFilePath();

    // An already open tab is the one to jump in: opening a second copy of the
    // same file would leave the user with two buffers to keep in sync.
    for (int i = 0; i < ui->tabWidget->count(); ++i)
    {
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(i));
        if (w && !w->fileName().isEmpty() &&
            QFileInfo(w->fileName()).absoluteFilePath() == absolute)
        {
            if (ui->contentSplitter->isVisible())
                ui->actionQuery_editor->activate(QAction::Trigger);
            ui->tabWidget->setCurrentIndex(i);
            gotoFilePosition(w, hit.line, hit.column, hit.length);
            w->setFocus();
            return;
        }
    }

    // The encoding the search decoded this file with, so that the tab shows the
    // very text the hit's line and column were measured in. The file dialog's
    // combo is *not* a substitute: it is empty until the user has opened Open or
    // Save at least once, and QueryWidget::openFile() refuses an encoding it
    // cannot name - which is what made this silently do nothing.
    QString encoding = hit.encoding;
    if (encoding.isEmpty())
        encoding = _fileDialog.encoding();
    if (encoding.isEmpty())
    {
        // First usable name from the settings, utf-8 as the last resort.
        const QStringList names = SqtSettings::value("encodings").toString()
                .split(',', Qt::SkipEmptyParts);
        for (const QString &n: names)
        {
            encoding = TextCodec::canonicalName(n.trimmed());
            if (!encoding.isEmpty())
                break;
        }
        if (encoding.isEmpty())
            encoding = QStringLiteral("UTF-8");
    }

    // A tab of its own, on a clone of the search's connection - the tab must be
    // able to run what it shows, and against the database the script belongs to.
    //
    // A remembered connection is handed over whatever its socket is doing. The
    // former test (opened, or a database already chosen) left a tab with no
    // connection at all whenever the link had dropped in the meantime, and such
    // a tab has no dbms: no keyword dictionary (the "emergency" colouring), no
    // database button, no way to reconnect. The clone carries the connection
    // string, the database and the dbms identity, so it highlights straight away
    // and its first query opens the link by itself.
    QueryWidget *w = (_searchConnection ?
                          new QueryWidget(_searchConnection->clone(), ui->tabWidget) :
                          new QueryWidget(ui->tabWidget));

    // Connected before the file is read: openFile() reports its failure through
    // these, and a tab deleted below would take the explanation with it.
    connect(w, &QueryWidget::sqlChanged, this, &MainWindow::sqlChanged);
    // anything not produced by a query run belongs to the log, not to the tab
    connect(w, &QueryWidget::message, this, &MainWindow::onMessage);
    connect(w, &QueryWidget::error, this, &MainWindow::onError);

    const int ind = ui->tabWidget->addTab(w, QString());
    ui->tabWidget->setCurrentIndex(ind);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    if (!w->openFile(hit.fileName, encoding))
    {
        ui->tabWidget->removeTab(ind);
        delete w;
        return;
    }

    w->setTitle(QFileInfo(hit.fileName).fileName());
    ui->tabWidget->setTabToolTip(ind, hit.fileName);
    w->highlight();
    w->setReadOnly(false);
    w->setModified(false);
    updateTabCaption(w);

    if (ui->contentSplitter->isVisible())
        ui->actionQuery_editor->activate(QAction::Trigger);
    gotoFilePosition(w, hit.line, hit.column, hit.length);
    w->setFocus();
}

void MainWindow::openPaneFileInEditor()
{
    if (!_paneHit)
        return;

    // The pane's own cursor, not the hit the preview was opened at: the file has
    // been read since, and the place under the cursor is the place the editor is
    // wanted for. A selection is carried over as it stands (its start and its
    // length), so a match just walked onto stays selected in the tab.
    FileSearchHit hit = *_paneHit;
    QTextCursor c = _objectScript->textCursor();
    if (!c.isNull())
    {
        const int start = qMin(c.position(), c.anchor());
        const QTextBlock block = _objectScript->document()->findBlock(start);
        if (block.isValid())
        {
            hit.line = block.blockNumber() + 1;
            hit.column = start - block.position() + 1;
            hit.length = (c.hasSelection() ? qAbs(c.position() - c.anchor()) : 0);
        }
    }
    openFileHitInEditor(hit);
}

