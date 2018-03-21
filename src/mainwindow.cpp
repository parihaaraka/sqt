#include <QTextCodec>
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "sqlsyntaxhighlighter.h"
#include "connectiondialog.h"
#include <QSettings>
#include <QMessageBox>
#include "dbobject.h"
#include "dbobjectsmodel.h"
#include "logindialog.h"
#include <QThread>
#include <QTimer>
#include "dbosortfilterproxymodel.h"
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include <QTextStream>
#include "querywidget.h"
#include <QTextDocumentFragment>
#include <QCloseEvent>
#include <QTextEdit>
#include "tablemodel.h"
#include "dbtreeitemdelegate.h"
#include "findandreplacepanel.h"
#include <memory>
#include "scripting.h"

#include <QDebug>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    qRegisterMetaType<QueryState>();

    setCentralWidget(ui->splitterV);
    _contextLabel.setFrameStyle(QFrame::StyledPanel);
    ui->statusBar->addPermanentWidget(&_contextLabel);
    _positionLabel.setVisible(false);
    ui->statusBar->addPermanentWidget(&_positionLabel);

    _objectScript = new QueryWidget(this);
    _objectScript->setReadOnly(true);
    ui->contentSplitter->insertWidget(0, _objectScript);
    ui->objectsView->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->objectsView->setItemDelegateForColumn(0, new DbTreeItemDelegate(this));
    ui->objectsView->setStyle(new MyProxyStyle);
    connect(ui->objectsView, &QTreeView::expanded, this, &MainWindow::objectsViewAdjustColumnWidth);
    connect(ui->objectsView, &QTreeView::collapsed, this, &MainWindow::objectsViewAdjustColumnWidth);

    DboSortFilterProxyModel *proxyModel = new DboSortFilterProxyModel(this);
    proxyModel->sort(0, Qt::AscendingOrder);
    _objectsModel = new DbObjectsModel();
    connect(_objectsModel, &DbObjectsModel::error, this, &MainWindow::onError);
    connect(_objectsModel, &DbObjectsModel::message, this, &MainWindow::onMessage);

    proxyModel->setSourceModel(_objectsModel);
    ui->objectsView->setModel(proxyModel);
    connect(ui->objectsView->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::selectionChanged);
    connect(ui->objectsView->selectionModel(), &QItemSelectionModel::currentChanged, this, &MainWindow::currentChanged);

    // it's about messages frame auto-hide
    _hideTimer = new QTimer(this);
    _hideTimer->setSingleShot(true);
    _hideTimer->setInterval(6000);
    ui->splitterV->setSizes({1000, 0});
    connect(ui->splitterV, &QSplitter::splitterMoved, [this]() { _hideTimer->stop(); });
    connect(_hideTimer, &QTimer::timeout, [this](){
        ui->splitterV->setSizes({1000, 0});
    });

    QFont font("Consolas, monospace, Menlo, Lucida Console, Liberation Mono, DejaVu Sans Mono, Bitstream Vera Sans Mono, Courier New, serif");
    font.setStyleHint(QFont::TypeWriter);
    ui->log->setFont(font);

    ui->actionRefresh->setShortcuts(QKeySequence::Refresh);
    ui->actionQuit->setShortcuts(QKeySequence::Quit);
    ui->actionNew->setShortcuts(QKeySequence::New);
    ui->actionOpen->setShortcuts(QKeySequence::Open);
    ui->actionSave->setShortcuts(QKeySequence::Save);
    ui->actionSave_as->setShortcuts(QKeySequence::SaveAs);
    ui->actionFind->setShortcuts(QKeySequence::Find);

    _frPanel = new FindAndReplacePanel();
    ui->menuEdit->addActions(_frPanel->actions());

    ui->objectsView->installEventFilter(this);
    refreshActions();

    QActionGroup *viewMode = new QActionGroup(this);
    viewMode->addAction(ui->actionObject_content);
    viewMode->addAction(ui->actionQuery_editor);
    connect(viewMode, &QActionGroup::triggered, this, &MainWindow::viewModeActionTriggered);
    connect(ui->actionQuit, &QAction::triggered, this, &MainWindow::close);
    ui->actionObject_content->setChecked(true);
    viewModeActionTriggered(ui->actionObject_content);

    // refresh root content (connection nodes from settings)
    _objectsModel->fillChildren();

    _tableModel = new TableModel(this);
    ui->tableView->setModel(_tableModel);
    ui->tableView->verticalHeader()->setDefaultSectionSize(ui->tableView->verticalHeader()->minimumSectionSize());
    ui->tableView->hide();
    //ui->tableView->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);

    QSettings settings;
    restoreGeometry(settings.value("mainWindowGeometry").toByteArray());
    restoreState(settings.value("mainWindowState").toByteArray());
    ui->splitterH->restoreState(settings.value("mainWindowHSplitter").toByteArray());
    ui->contentSplitter->restoreState(settings.value("mainWindowScriptSplitter").toByteArray());
    _fileDialog.restoreState(settings.value("fileDialog").toByteArray());
    _mruDirs = settings.value("mruDirs").toStringList();
    // make previously used directory to be current
    if (!_mruDirs.isEmpty())
        _fileDialog.setDirectory(_mruDirs.last());
}

MainWindow::~MainWindow()
{
    delete _frPanel;
    delete _objectsModel;
    delete ui;
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
    QSettings settings;
    settings.setValue("mainWindowGeometry", saveGeometry());
    settings.setValue("mainWindowState", saveState());
    settings.setValue("mainWindowHSplitter", ui->splitterH->saveState());
    settings.setValue("mainWindowScriptSplitter", ui->contentSplitter->saveState());
    settings.setValue("fileDialog", _fileDialog.saveState());
    settings.setValue("mruDirs", _mruDirs);
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
                       tr("sql query tool v%1<br/>&copy; 2013-2018 Andrey Lukyanov<br/>freeware<br/><br/>"
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

        std::shared_ptr<DbConnection> con = DbConnectionFactory::createConnection(QString::number(std::intptr_t(obj)), cs);
        //connect(con.get(), &DbConnection::setContext, this, &MainWindow::refreshContextInfo, Qt::UniqueConnection);
        //QString messages;
        //QMetaObject::Connection errConnection =
        connect(con.get(), &DbConnection::error, this, &MainWindow::onError);
        connect(con.get(), &DbConnection::message, this, &MainWindow::onMessage);
        if (!con->open())
        {
            con->disconnect();
            // show connection error in place of node content
            //_objectsModel->setData(currentNodeIndex, messages, DbObject::ContentRole);
            //_objectsModel->setData(currentNodeIndex, "text", DbObject::ContentTypeRole);
            //fillContent(currentNodeIndex, con);
        }
        else
        {
            if (user != newUser && !newUser.isEmpty())
            {
                obj->setData(newUser, DbObject::NameRole);
                _objectsModel->saveConnectionSettings();
            }
            //con->disconnect(errConnection);
            _objectsModel->setData(currentNodeIndex, true, DbObject::ParentRole);
            _objectsModel->setData(currentNodeIndex, con->dbmsInfo(), DbObject::ContentRole);
            _objectsModel->setData(currentNodeIndex, "text", DbObject::ContentTypeRole);
            fillContent(currentNodeIndex, con);
            scriptSelectedObjects(true);
        }
    }
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
        if (con && con->isOpened()) // disconnect
        {
            DbObject *item = static_cast<DbObject*>(srcIndex.internalPointer());
            _objectsModel->removeRows(0, item->childCount(), srcIndex);
            //DbConnection *con = _objectsModel->dbConnection(srcIndex);
            con->close();
            con->disconnect(); // disconnect all slots from all signals
            _objectsModel->setData(srcIndex, false, DbObject::ParentRole);
            _objectsModel->setData(srcIndex, QString(), DbObject::ContentRole);
            _objectScript->clear();
        }
        else
        {
            on_objectsView_activated(index);
        }
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
    DbConnection *cn = _objectsModel->dbConnection(nodeToRefresh).get();
    Scripting::refresh(cn, Scripting::Context::Root);
    Scripting::refresh(cn, Scripting::Context::Content);
    Scripting::refresh(cn, Scripting::Context::Preview);

    // clear all child nodes
    _objectsModel->removeRows(0, item->childCount(), nodeToRefresh);

    // if parrent still contains current node
    if (_objectsModel->fillChildren(nodeToRefresh))
    {
        // clear current node's script
        item->setData(QVariant(), DbObject::ContentRole);
        // refresh current node's script
        scriptSelectedObjects(true);
    }
    else
    {
        // TODO ensure this is needed
        DboSortFilterProxyModel *proxyModel = qobject_cast<DboSortFilterProxyModel*>(ui->objectsView->model());
        proxyModel->invalidate();
    }
}

void MainWindow::on_actionChange_sort_mode_triggered()
{
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
    QItemSelectionModel *selectionModel = qobject_cast<QItemSelectionModel*>(sender());
    QModelIndexList si = selectionModel->selectedIndexes();
    QModelIndex cur = selectionModel->currentIndex();
    if (!cur.isValid())
        return;
    if (si.count() > 1 || deselected.indexes().count() > 1)
    {
        bool multiselect = (cur.parent().isValid() && cur.parent().data(DbObject::MultiselectRole).toBool());
        QString children;
        foreach(QModelIndex i, si)
        {
            if (i.parent() != cur.parent() || (i != cur && !multiselect) || !i.data(DbObject::IdRole).isValid())
                selectionModel->select(i, QItemSelectionModel::Deselect);
            else if (multiselect && i.data(DbObject::IdRole).isValid())
                children += (children.length() > 0 ? "," : "") + i.data(DbObject::IdRole).toString();
        }
        scriptSelectedObjects(si.count() == 1);
    }
}

void MainWindow::currentChanged(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous)
    ui->actionNew->setEnabled(current.isValid());
    ui->actionRefresh->setStatusTip(current.isValid() ? QTextEdit(current.data().toString()).toPlainText() : "");
    ui->actionRefresh->setEnabled(current.isValid());
    ui->actionChange_sort_mode->setEnabled(current.isValid());
    refreshContextInfo();

    QItemSelectionModel *selectionModel = qobject_cast<QItemSelectionModel*>(sender());
    if (selectionModel->selectedIndexes().count() == 1)
        scriptSelectedObjects();
}

void MainWindow::viewModeActionTriggered(QAction *action)
{
    setUpdatesEnabled(false);
    ui->contentSplitter->setVisible(action == ui->actionObject_content);
    ui->tabWidget->setVisible(action == ui->actionQuery_editor);
    ui->actionObject_content->setShortcut(action == ui->actionQuery_editor ? Qt::Key_F2 : 0);
    ui->actionQuery_editor->setShortcut(action == ui->actionObject_content ? Qt::Key_F2 : 0);
    setUpdatesEnabled(true);
    if (ui->tabWidget->isHidden())
        scriptSelectedObjects();
}

void MainWindow::on_actionExecute_query_triggered()
{
    QueryWidget *q = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    DbConnection *con = q->dbConnection();
    if (!con)
        return;
    if (con->queryState() == QueryState::Running)
    {
        con->cancel();
    }
    else if (con->queryState() == QueryState::Inactive)
    {
        q->clearResult();
        QString query = (q->textCursor().hasSelection() ?
                             q->textCursor().selection().toPlainText() :
                             q->toPlainText());
        if (query.isEmpty())
            return;

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
    default: ; // to avoid a bunch of warnigs
    }
    return QObject::eventFilter(object, event);
}

bool MainWindow::closeTab(int index)
{
    if (ensureSaved(index, false, true))
    {
        QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->widget(index));
        DbConnection *con = w->dbConnection();
        if (con && con->queryState() != QueryState::Inactive)
            return false;
        ui->tabWidget->removeTab(index);
        delete w;
        refreshContextInfo();
        return true;
    }
    return false;
}

void MainWindow::on_actionNew_triggered()
{
/*
    QueryWidget *w = new QueryWidget(QString(), ui->tabWidget);
    int ind = ui->tabWidget->addTab(w, QString());
    ui->tabWidget->setCurrentIndex(ind);
    w->queryEditor()->installEventFilter(this);
    w->queryEditor()->document()->setModified(false);
    connect(w->queryEditor(), &QTextEdit::cursorPositionChanged, this, &MainWindow::refreshCursorInfo);
    connect(w->queryEditor(), &QTextEdit::selectionChanged, this, &MainWindow::refreshCursorInfo);
    connect(w, &QueryWidget::sqlChanged, this, &MainWindow::sqlChanged);
*/

    QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->
            mapToSource(ui->objectsView->currentIndex());
    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(srcIndex);
    if (!con)
    {
        onError("db connection unavailable");
        return;
    }

    QueryWidget *w;
    // create db-connected editor
    if (con->isOpened() || !con->database().isEmpty())
    {
        DbConnection *cn = con->clone();
        w = new QueryWidget(cn, ui->tabWidget);
        connect(cn, &DbConnection::setContext, this, &MainWindow::refreshContextInfo);
        connect(cn, &DbConnection::queryStateChanged, this, &MainWindow::refreshActions);
    }
    // create editor without query execution ability
    // (broken by previous opened connection check)
    // * do i need it?
    else
    {
        w = new QueryWidget(ui->tabWidget);
    }

    int ind = ui->tabWidget->addTab(w, QString());
    ui->tabWidget->setCurrentIndex(ind);

    if (_objectsModel->data(srcIndex, DbObject::ContentTypeRole).toString() == "script")
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
    if (!caption.endsWith("*") && isModified)
    {
        ui->tabWidget->setTabText(ind, fn + " *");
    }
    else if (!isModified && caption.endsWith("*"))
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
    if (!_fileDialog.exec())
        return;
    QString fn = _fileDialog.selectedFiles().at(0);
    adjustMruDirs();

    int tabs_count = ui->tabWidget->count();
    ui->actionNew->activate(QAction::Trigger);
    if (ui->tabWidget->count() != tabs_count)
    {
        QueryWidget *w = currentQueryWidget();
        QApplication::setOverrideCursor(Qt::WaitCursor);
        ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);
        w->openFile(fn, _fileDialog.encoding());
        ui->tabWidget->setTabText(ui->tabWidget->currentIndex(), QFileInfo(fn).fileName());
        ui->tabWidget->setTabToolTip(ui->tabWidget->currentIndex(), fn);
    }
}

QueryWidget *MainWindow::currentQueryWidget()
{
    if (!ui->tabWidget->count())
        return 0;
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
                _fileDialog.setEncoding(w->encoding());
                _fileDialog.setHistory(_mruDirs);
                if (!_fileDialog.exec())
                    return false;
                encoding = _fileDialog.encoding();
                fn = _fileDialog.selectedFiles().at(0);
                adjustMruDirs();
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
    QWidget *fw = QApplication::focusWidget();
    QueryWidget *w = qobject_cast<QueryWidget*>(ui->tabWidget->currentWidget());
    ui->actionExecute_query->setEnabled(fw != ui->objectsView && ui->tabWidget->count() && w->dbConnection());
    QueryState qState = (w && w->dbConnection() ? w->dbConnection()->queryState() : QueryState::Inactive);
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

    ui->actionRefresh->setEnabled(ui->objectsView->hasFocus());
    ui->actionChange_sort_mode->setEnabled(ui->actionRefresh->isEnabled());

    QueryWidget *qw = (ui->tabWidget->isHidden() ? _objectScript : w);
    foreach(QAction *action, ui->menuEdit->actions())
        action->setEnabled(qw);
    _frPanel->setEditor(qw);
}

bool MainWindow::script(const QModelIndex &index, const QString &children)
{
    //QString result;
    if (!index.isValid())
        return false;

    DbObject *parentItem = static_cast<DbObject*>(index.internalPointer());
    QString type = parentItem->data(DbObject::TypeRole).toString();

    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(index);
    try
    {
        if (!con->open())
            return false;
        Scripting::Script *s = Scripting::getScript(con.get(), Scripting::Context::Content, type);
        if (!s)
            throw tr("script to make %1 content not found").arg(type);

        QString query = s->body;
        QRegularExpression expr("\\$(\\w+\\.\\w+)\\$");
        QRegularExpressionMatchIterator i = expr.globalMatch(query);
        QStringList macros;
        while (i.hasNext())
        {
            QRegularExpressionMatch match = i.next();
            if (!macros.contains(match.captured(1))) macros << match.captured(1);
        }
        foreach (QString macro, macros)
        {
            QString value;
            // TODO children.names ?
            if (macro == "children.ids")
                value = children.isEmpty() ? "-1" : children;
            else
                value = _objectsModel->parentNodeProperty(index, macro).toString();
            query = query.replace("$" + macro + "$", value);
        }

        if (s->type == Scripting::Script::Type::SQL)
        {
            con->execute(query);
        }
        else if (s->type == Scripting::Script::Type::QS)
        {

        }
        return true;
    }
    catch (const QString &err)
    {
        onError(err);
    }
    return false;
}

void MainWindow::adjustMruDirs()
{
    // erase current dir if exists
    _mruDirs.removeAll(_fileDialog.directory().absolutePath());
    // put current dir on top
    _mruDirs.append(_fileDialog.directory().absolutePath());
    // keep last 10 mru paths
    while (_mruDirs.size() > 10) // TODO move to settings
        _mruDirs.removeAt(0);
    _fileDialog.setHistory(_mruDirs);
}

void MainWindow::scriptSelectedObjects(bool currentOnly)
{
    if (!_objectScript->isVisible() && !ui->tableView->isVisible())
        return;
    _tableModel->clear();
    QModelIndex srcIndex = static_cast<QSortFilterProxyModel*>(ui->objectsView->model())->mapToSource(ui->objectsView->currentIndex());
    if (!srcIndex.isValid())
    {
        _objectScript->clear();
        ui->tableView->hide();
        return;
    }
    QItemSelectionModel *selectionModel = qobject_cast<QItemSelectionModel*>(ui->objectsView->selectionModel());
    QModelIndexList si = selectionModel->selectedIndexes();

    std::shared_ptr<DbConnection> con = _objectsModel->dbConnection(srcIndex);
    if (!con)
        return;
    con->clearResultsets();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    ScopeGuard<void(*)()> cursorGuard(QApplication::restoreOverrideCursor);

    try
    {
        con->open();
        if (si.count() > 1 && !currentOnly)
        {
            // the only reason to allow multiple selection is to script all selected objects
            // within parent script (e.g. select/insert table scripts with selected columns only)
            QString children;
            foreach(QModelIndex i, si)
            {
                if (i.data(DbObject::IdRole).isValid())
                    children += (children.length() > 0 ? "," : "") + i.data(DbObject::IdRole).toString();
            }
            script(srcIndex.parent(), children);
            QModelIndex tmp_index;
            fillContent(tmp_index, con);
        }
        else
        {
            QString typeRole = _objectsModel->data(srcIndex, DbObject::TypeRole).toString();
            QVariant scriptText = srcIndex.data(DbObject::ContentRole);
            if (!scriptText.isValid() && Scripting::getScript(con.get(), Scripting::Context::Content, typeRole) != nullptr)
            {
                script(srcIndex);
            }
            fillContent(srcIndex, con);

            Scripting::Script *s = Scripting::getScript(con.get(), Scripting::Context::Preview, typeRole);
            if (s != nullptr)
            {
                // TODO
                //script(srcIndex);

                QString objName = _objectsModel->data(srcIndex, DbObject::NameRole).toString();
                con->execute("select * from " + objName, nullptr, 100);
                if (!con->_resultsets.isEmpty())
                {
                    _tableModel->take(con->_resultsets.front());
                    ui->tableView->show();
                    ui->tableView->resizeColumnsToContents();
                }
            }
            else if (con->_resultsets.isEmpty())
                ui->tableView->hide();
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

void MainWindow::fillContent(QModelIndex &index, std::shared_ptr<DbConnection> con)
{
    _objectScript->clear();
    _tableModel->clear();

    ui->tableView->hide();
    _objectScript->show();

    QVariant value;
    QVariant type;
    if (index.isValid())
    {
        value = _objectsModel->data(index, DbObject::ContentRole);
        type = _objectsModel->data(index, DbObject::ContentTypeRole);
        if (value.isValid())
        {
            fillContent(value, type, con);
            return;
        }
    }
    DataTable *table = con->_resultsets.isEmpty() ? nullptr : con->_resultsets.front();
    if (table)
    {
        if (table->columnCount() > 1 || table->rowCount() > 1)
        {
            ui->tableView->show();
            _objectScript->hide();
            _tableModel->take(table);
            ui->tableView->resizeColumnsToContents();
            if (index.isValid())
                _objectsModel->setData(index, "table", DbObject::ContentTypeRole);
            return;
        }
        else
        {
            if (table->columnCount() == 1)
            {
                value = table->getRow(0)[0].toString();
                type = table->getColumn(0).name();
            }
        }
    }
    /*else if (!con->lastError().isEmpty())
    {
        value = con->lastError();
        type = "text";
    }*/
    if (value.isValid())
    {
        if (index.isValid())
        {
            _objectsModel->setData(index, value, DbObject::ContentRole);
            _objectsModel->setData(index, type, DbObject::ContentTypeRole);
        }
        fillContent(value, type, con);
    }
}

void MainWindow::fillContent(QVariant &value, QVariant &type, std::shared_ptr<DbConnection> con)
{
    if (!value.isValid())
        return;

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

    DbConnection *con = 0;
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

    if (con)
    {
        _contextLabel.setText(con->context());
    }
    else
    {
        _contextLabel.clear();
    }
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
    refreshActions();
    refreshContextInfo();
    refreshCursorInfo();
}

void MainWindow::log(const QString &msg)
{
    ui->log->appendPlainText(QString("%1: %2")
            .arg(QTime::currentTime().toString(Qt::ISODateWithMs))
            .arg(msg));

    int blockCount = ui->log->document()->blockCount();
    if (blockCount > 100)
    {
        auto b = ui->log->document()->findBlockByNumber(blockCount - 101);
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
