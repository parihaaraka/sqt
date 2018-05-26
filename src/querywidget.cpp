#include "querywidget.h"
#include <QTabWidget>
#include <QApplication>
#include "sqlsyntaxhighlighter.h"
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include <QTableView>
#include <QHeaderView>
#include "tablemodel.h"
#include <QFile>
#include <QMessageBox>
#include <QTextCodec>
#include <QTextStream>
#include <QMenu>
#include <QClipboard>
#include <QVBoxLayout>
#include "findandreplacepanel.h"
#include <QKeyEvent>
#include <QMimeData>
#include <memory>
#include <QJsonDocument>
#include <QJsonObject>
#include "dbobjectsmodel.h"
#include "mainwindow.h"
#include "scripting.h"
#include <QDesktopServices>
#include "codeeditor.h"
#include "settings.h"
#include <QTextBrowser>

QueryWidget::QueryWidget(QWidget *parent) : QueryWidget(nullptr, parent)
{
}

QueryWidget::QueryWidget(DbConnection *connection, QWidget *parent) :
    QSplitter(parent), _editor(nullptr)
{
    _messages = nullptr;
    _highlighter = nullptr;

    //_resultMenu = new QMenu(this);
    //_actionCopy = new QAction(tr("Copy CSV"), this);
    //_actionCopy->setShortcuts(QKeySequence::Copy);
    //_resultMenu->addAction(_actionCopy);
    //connect(_actionCopy, &QAction::triggered, this, &QueryWidget::onActionCopyTriggered);

    /*_actionCopyHTML = new QAction(tr("Copy html"), this);
    _resultMenu->addAction(_actionCopyHTML);
    connect(_actionCopyHTML, &QAction::triggered, this, &QueryWidget::on_actionCopy_triggered);*/

    _editorLayout = new QVBoxLayout(this);
    _editorLayout->setSpacing(0);
    _editorLayout->setMargin(0);
    QWidget *w = new QWidget(this);
    w->setLayout(_editorLayout);
    addWidget(w);
    setDbConnection(connection);
}

QueryWidget::~QueryWidget()
{
    clearResult();
    if (_editorLayout->count() > 1)
    {
        QWidget *w = _editorLayout->itemAt(1)->widget();
        _editorLayout->removeWidget(w);
        w->setParent(0);
    }
    if (_highlighter)
    {
        _highlighter->setDocument(0);
        delete _highlighter;
        _highlighter = nullptr;
    }
}

void QueryWidget::openFile(const QString &fileName, const QString &encoding)
{
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        QMessageBox::critical(this, tr("Error"), f.errorString());
        return;
    }
    _fn = fileName;
    _encoding = encoding;
    QTextStream read_stream(&f);
    read_stream.setCodec(QTextCodec::codecForName(_encoding.toLatin1().data()));
    if (f.size() > 1024 * 1024 * 5) // do not highlight > 5mb scripts
        dehighlight();
    else
        highlight();
    setPlainText(read_stream.readAll());
    document()->setModified(false);
    f.close();
}

bool QueryWidget::saveFile(const QString &fileName, const QString &encoding)
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly))
    {
        QMessageBox::critical(this, tr("Error"), f.errorString());
        return false;
    }
    _fn = fileName;
    if (!encoding.isEmpty()) _encoding = encoding;
    QTextStream save_stream(&f);
    save_stream.setCodec(QTextCodec::codecForName(_encoding.toLatin1().data()));
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        save_stream << plain->toPlainText();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        save_stream << rich->toPlainText();
    save_stream.flush();
    f.close();
    return true;
}

void QueryWidget::setDbConnection(DbConnection *connection)
{
    _connection.reset(connection);
    if (connection)
    {
        if (!findChild<QTabWidget*>("results"))
        {
            QTabWidget *res = new QTabWidget(this);
            res->setObjectName("results");
            res->setDocumentMode(true);
            _resSplitter = new QSplitter(res);
            _messages = new QPlainTextEdit(res);
            _messages->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            res->addTab(_messages, tr("messages"));
            addWidget(res);
            setSizes(QList<int>() << 1 << 0);
            setOrientation(Qt::Vertical);
        }
        connect(connection, &DbConnection::fetched, this, &QueryWidget::fetched, Qt::QueuedConnection);
        connect(connection, &DbConnection::message, this, &QueryWidget::onMessage, Qt::QueuedConnection);
        connect(connection, &DbConnection::error, this, &QueryWidget::onError, Qt::QueuedConnection);
        connect(connection, &DbConnection::queryStateChanged, this, [this]() {
            if (_connection->queryState() == QueryState::Inactive)
            {
                onMessage(tr("%1: done in %2").arg(QTime::currentTime().toString("HH:mm:ss")).arg(_connection->elapsed()));

                // print all resultsets structure ready to be used in 'create function returning table(...)'
                QColor resultsetStructureColor = _messages->palette().text().color();
                resultsetStructureColor.setAlphaF(0.6);
                for (const auto res: _connection->_resultsets)
                {
                    if (!res->columnCount())
                        continue;
                    QString structure;
                    for (int i = 0; i < res->columnCount(); ++i)
                    {
                        auto &c = res->getColumn(i);
                        QString typeDescr = c.typeName();

                        if (i)
                            structure += ", ";
                        structure += _connection->escapeIdentifier(c.name()) + ' ' + typeDescr;
                    }
                    log('(' + structure + ')', resultsetStructureColor);
                }

                // scroll down and left
                auto cursor = _messages->textCursor();
                cursor.movePosition(QTextCursor::End);
                cursor.movePosition(QTextCursor::StartOfLine);
                _messages->setTextCursor(cursor);
            }
        }, Qt::QueuedConnection);
        connection->open();
    }
    highlight(_connection);
}

void QueryWidget::ShowFindPanel(FindAndReplacePanel *panel)
{
    if (_editorLayout->count() > 1 && _editorLayout->itemAt(1)->widget() != panel)
        _editorLayout->removeWidget(_editorLayout->itemAt(1)->widget());
    if (_editorLayout->count() == 1)
        _editorLayout->addWidget(panel);
    panel->show();
    panel->setEditor(this);
    panel->setFocus();
}

void QueryWidget::highlight(std::shared_ptr<DbConnection> con)
{
    if (_connection != con || !_highlighter)
    {
        if (con)
            _connection = con;
        QJsonDocument settingsDocument;
        QJsonObject settings;
        if (_connection)
        {
            try
            {
                QFile file(Scripting::dbmsScriptPath(_connection.get()) + "hl.conf");
                if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                {
                    auto text_data = file.readAll();
                    if (!text_data.isEmpty())
                    {
                        QJsonParseError error;
                        settingsDocument = QJsonDocument::fromJson(text_data, &error);
                        if (settingsDocument.isNull())
                            throw error.errorString();
                        settings = settingsDocument.object();
                    }
                    file.close();
                }
            }
            catch (const QString &err)
            {
                emit error(err);
            }
        }

        if (_highlighter)
        {
            _highlighter->setDocument(0);
            delete _highlighter;
            _highlighter = nullptr;
        }

        if (_editor)
            _highlighter = new SqlSyntaxHighlighter(settings, _editor);
    }

    if (_highlighter)
        _highlighter->setDocument(document());
}

void QueryWidget::dehighlight()
{
    if (_highlighter)
        _highlighter->setDocument(0);
}

void QueryWidget::setReadOnly(bool ro)
{
    if (!_editor)
        return;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        plain->setReadOnly(ro);
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        rich->setReadOnly(ro);
}

bool QueryWidget::isReadOnly() const
{
    if (!_editor)
        return false;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        return plain->isReadOnly();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        return rich->isReadOnly();
    return false;
}

void QueryWidget::clear()
{
    if (!_editor)
        return;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        plain->clear();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        rich->clear();
}

bool QueryWidget::isModified() const
{
    QTextDocument *doc = document();
    return (doc ? doc->isModified() : false);
}

void QueryWidget::setModified(bool m)
{
    QTextDocument *doc = document();
    if (doc)
        doc->setModified(m);
}

void QueryWidget::setTextCursor(const QTextCursor &cursor)
{
    if (!_editor)
        return;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        plain->setTextCursor(cursor);
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        rich->setTextCursor(cursor);
}

QString QueryWidget::toPlainText()
{
    if (!_editor)
        return QString();
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        return plain->toPlainText();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        return rich->toPlainText();
    return QString();
}

QTextCursor QueryWidget::textCursor() const
{
    if (!_editor)
        return QTextCursor();
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        return plain->textCursor();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        return rich->textCursor();
    return QTextCursor();
}

QTextDocument* QueryWidget::document() const
{
    if (!_editor)
        return nullptr;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        return plain->document();
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        return rich->document();
    return nullptr;
}

QWidget *QueryWidget::editor() const
{
    return _editor;
}

template<class T>
T* initEditor(QWidget **textEdit, QueryWidget *parent)
{
    T *editor = qobject_cast<T*>(*textEdit);
    if (editor)
        return editor;

    editor = new T(parent);
    editor->setObjectName("sql");
    editor->installEventFilter(parent);
    editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);

    //_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    //connect(_editor, &QTextEdit::customContextMenuRequested, this, &QueryWidget::on_customEditorContextMenuRequested);

    QObject::connect(editor->document(), &QTextDocument::contentsChanged, parent, &QueryWidget::sqlChanged);
    QObject::connect(editor, &T::cursorPositionChanged, parent, &QueryWidget::onCursorPositionChanged);
    QObject::connect(editor, &T::textChanged, parent, &QueryWidget::onCursorPositionChanged);

    if (MainWindow *mainWindow = qobject_cast<MainWindow*>(parent->window()))
        QObject::connect(editor, &T::cursorPositionChanged, mainWindow, &MainWindow::refreshCursorInfo);

    auto l = parent->widget(0)->layout();
    if (*textEdit)
        delete l->replaceWidget(*textEdit, editor);
    else
        qobject_cast<QVBoxLayout*>(l)->insertWidget(0, editor);
    *textEdit = qobject_cast<QWidget*>(editor);
    parent->setFocusProxy(editor);
    return editor;
}

void QueryWidget::setPlainText(const QString &text)
{
    CodeEditor *editor = initEditor<CodeEditor>(&_editor, this);
    editor->setPlainText(text);
}

void QueryWidget::setHtml(const QString &html)
{
    //QTextEdit *editor = initEditor<QTextEdit>(&_editor, this);
    QTextBrowser *editor = initEditor<QTextBrowser>(&_editor, this);
    editor->setOpenExternalLinks(true);
    editor->setHtml(html);
}

void QueryWidget::log(const QString &text, QColor color)
{
    QTextCharFormat fmt = _messages->currentCharFormat();
    fmt.setForeground(QBrush(color));
    _messages->mergeCurrentCharFormat(fmt);

    _messages->appendPlainText(text.trimmed());
    if (!text.isEmpty() && widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);
}

void QueryWidget::onMessage(const QString &text)
{
    log(text, Qt::black);
}

void QueryWidget::onError(const QString &text)
{
    log(text, Qt::red);
}

void QueryWidget::fetched(DataTable *table)
{
    if (widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);

    // second widget within splitter is a tabwidget with persistent log tab
    // and optional (leading) resultsets tab
    QTabWidget *res_tw = qobject_cast<QTabWidget*>(widget(1));
    Q_ASSERT(res_tw != nullptr);
    if (res_tw->count() == 1) // only log tab exists
    {
        res_tw->insertTab(0, _resSplitter, tr("resultsets"));
        res_tw->setCurrentIndex(0);
    }

    QString tname = QString::number(std::intptr_t(table));
    QTableView *tv = nullptr;
    TableModel *m = nullptr;
    if (_resSplitter->count())
        tv = qobject_cast<QTableView*>(_resSplitter->widget(_resSplitter->count() - 1));
    if (!tv || tv->objectName() != tname)
    {
        tv = new QTableView(_resSplitter);
        tv->horizontalHeader()->viewport()->setMouseTracking(true);
        tv->setObjectName(tname);

        //tv->setContextMenuPolicy(Qt::CustomContextMenu);
        //connect(tv, &QTableView::customContextMenuRequested, this, &QueryWidget::onCustomGridContextMenuRequested);
        //tv->setSelectionMode(QAbstractItemView::ContiguousSelection);
        //tv->addAction(_actionCopy);

        m = new TableModel(_resSplitter);
        _tables.append(m);
        tv->setModel(m);
        _resSplitter->addWidget(tv);
        m->take(table);
        // prevent autoresize overhead when big resultset is fetched at once
        tv->horizontalHeader()->setResizeContentsPrecision(20);
        tv->resizeColumnsToContents();
    }
    else
    {
        m = qobject_cast<TableModel*>(tv->model());
        m->take(table);
    }
}

void QueryWidget::clearResult()
{
    if (!_connection)
        return;

    if (_messages) // it was nullptr once.. can't reproduce
        _messages->clear();

    if (count() > 1) // TabWidget exists (false on destruction)
    {
        QTabWidget *res_tw = qobject_cast<QTabWidget*>(widget(1));
        if (res_tw && res_tw->count() > 1)
        {
            // remove resultsets tab, _resSplitter stays alive
            res_tw->removeTab(0);
            // delete QTableView widgets (current shown resultsets)
            // (it looks like simple delete works ok instead of setParent(nullptr) and deleteLater())
            for (int i = _resSplitter->count() - 1; i >= 0; --i)
                delete _resSplitter->widget(i);
        }
    }
    qDeleteAll(_tables);
    _tables.clear();
}

/*
void QueryWidget::onCustomGridContextMenuRequested(const QPoint &pos)
{
    QTableView *tv = qobject_cast<QTableView*>(sender());
    if (!tv || !tv->model()->rowCount())
        return;

    _resultMenu->exec(tv->mapToGlobal(pos));
}
*/

/*
void QueryWidget::on_customEditorContextMenuRequested(const QPoint &pos)
{
    QTextEdit *ed = qobject_cast<QTextEdit*>(sender());
    QMenu *menu = ed->createStandardContextMenu(pos);
    if (ed->actions().count())
    {
        menu->addSeparator();
        menu->addActions(ed->actions());
    }
    menu->exec(ed->mapToGlobal(pos));
    delete menu;
}
*/

/*
void QueryWidget::onActionCopyTriggered()
{
    QTableView *tv = qobject_cast<QTableView*>(QApplication::focusWidget());
    if (!tv)
        return;
    bool isHTML = false; //(sender() == _actionCopyHTML);
    QString result = (isHTML ? "<table><tr>" : "");
    TableModel *m = qobject_cast<TableModel*>(tv->model());

    QModelIndexList indexes = tv->selectionModel()->selectedIndexes();
    if(indexes.size() < 1)
        return;
    qSort(indexes);

    QModelIndex prev;
    for(QModelIndex &cur: indexes)
    {
        //QMetaType::Type t = (QMetaType::Type)m->data(cur).type();
        QString val = m->data(cur).toString();
        if (prev.isValid())
        {
            if (cur.row() != prev.row())
                result.append(isHTML ? QString("</tr><tr>") : QString(QChar::CarriageReturn));
            else if (!isHTML)
                result.append(',');
        }

        if (isHTML)
            result.append("<td>" + val.toHtmlEscaped() + "</td>");
        else
        {
            int type = m->table()->getColumn(cur.column()).sqlType();
            result.append(_connection->isUnquotedType(type) ? val : "\"" + val.replace("\"", "\"\"") + "\"");
        }
        prev = cur;
    }
    result.append(isHTML ? QString("</tr></table>") : QString(QChar::CarriageReturn));
    if (isHTML)
    {
        QMimeData *data = new QMimeData;
        data->setHtml(result);
        QApplication::clipboard()->setMimeData(data);
    }
    else
    {
        QApplication::clipboard()->setText(result);
    }
}
*/

void QueryWidget::onCursorPositionChanged()
{
    QList<QTextEdit::ExtraSelection> left_bracket;
    QList<QTextEdit::ExtraSelection> right_bracket_and_current_line;
    setExtraSelections(left_bracket); // clear extra selections

    /*  current line distinct background
    if (!isReadOnly())
    {
        QTextEdit::ExtraSelection selection;
        QColor lineColor = QColor("#efefef");
        selection.format.setBackground(lineColor);
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();
        right_bracket_and_current_line.append(selection);
    }*/

    QTextCursor cursor = textCursor();
    if (cursor.selectedText().length() > 1 || !_highlighter || !_highlighter->document())
    {
        setExtraSelections(right_bracket_and_current_line);
        return;
    }

    QTextCursor c(cursor);
    c.clearSelection();
    if (c.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor))
    {
        // get bracket to the left and matching one selected
        left_bracket.append(matchBracket(c));
        c.movePosition(QTextCursor::NextCharacter);
    }
    if (c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor))
    {
        // get bracket to the right and matching one selected
        right_bracket_and_current_line.append(matchBracket(c, left_bracket.empty() ? 100 : 115));
    }
    setExtraSelections(right_bracket_and_current_line + left_bracket);
}

bool QueryWidget::isEnveloped(const QTextCursor &c)
{
    int pos = c.positionInBlock();
    if (c.hasSelection() && c.anchor() < c.position())
        pos -= 1;
    for (const QTextLayout::FormatRange &r: c.block().layout()->formats())
    {
        if (pos >= r.start && pos < r.start + r.length)
        {
            if (r.format.property(QTextFormat::UserProperty) == "envelope")
                return true;
            break;
        }
    }
    return false;
}

void QueryWidget::setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections)
{
    if (!_editor)
        return;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
        plain->setExtraSelections(selections);
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        rich->setExtraSelections(selections);
}

QList<QTextEdit::ExtraSelection> QueryWidget::matchBracket(const QTextCursor &selectedBracket, int darkerFactor)
{
    QList<QTextEdit::ExtraSelection> selections;

    QChar c1 = selectedBracket.selectedText()[0];
    QString brackets("([{)]}");
    int c1pos = brackets.indexOf(c1, Qt::CaseInsensitive);
    if (c1pos == -1 || isEnveloped(selectedBracket))
        return selections;
    QChar c2 = (c1pos < 3 ? brackets[c1pos + 3] : brackets[c1pos - 3]);

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(160,255,160).darker(darkerFactor));
    selection.cursor = selectedBracket;
    selections.append(selection);

    QTextCursor match_cur(selectedBracket);
    int counter = 1;
    QRegularExpression pattern(QString("[\\%1\\%2]").arg(c1).arg(c2));
    QTextDocument::FindFlags flags =
            (c1pos < 3 ? QTextDocument::FindCaseSensitively :
                         QTextDocument::FindBackward | QTextDocument::FindCaseSensitively);
    do
    {
        if (c1pos < 3)
            match_cur.movePosition(QTextCursor::NextCharacter);

        match_cur = document()->find(pattern, match_cur, flags);
        if (match_cur.isNull())
            break;

        if (isEnveloped(match_cur))
            continue;

        QChar tmp = match_cur.selectedText().at(0);
        counter += (tmp == c1 ? 1 : -1);
        if (counter == 0)
        {
            selection.cursor = match_cur;
            selections.append(selection);
        }
    }
    while (counter);
    if (counter)
        selections[0].format.setBackground(QColor(255,160,160).darker(darkerFactor));
    return selections;
}

bool QueryWidget::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type())
    {
    case QEvent::KeyPress:
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        QPlainTextEdit *e = qobject_cast<QPlainTextEdit*>(object);
        if (!e || e->isReadOnly())
            break;
        if (keyEvent->key() == Qt::Key_Insert && keyEvent->modifiers() == Qt::NoModifier)
        {
            e->setOverwriteMode(!e->overwriteMode());
            return true;
        }

        if (keyEvent->key() == Qt::Key_F1)
        {
            const QStringList &URIs =
                    (QLocale::system().language() == QLocale::Russian ?
                         QStringList {
                         "https://postgrespro.ru/docs/postgresql/current/functions",
                         "https://postgrespro.ru/docs/postgresql/current/sql-commands"
                        } :
                         QStringList {
                         "https://www.postgresql.org/docs/current/static/functions.html",
                         "https://www.postgresql.org/docs/current/static/sql-commands.html"
                        }
                    );
            int ind = keyEvent->modifiers().testFlag(Qt::ShiftModifier) ? 0 : 1;
            QDesktopServices::openUrl(QUrl(URIs[ind]));
            return true;
        }

        QTextCursor c = e->textCursor();

        if (keyEvent->key() == Qt::Key_M && keyEvent->modifiers().testFlag(Qt::ControlModifier))
        {
            CodeBlockProperties *prop = static_cast<CodeBlockProperties*>(c.block().userData());
            c.block().setUserData(prop ? nullptr : new CodeBlockProperties(this));
        }

        if (keyEvent->key() == Qt::Key_Return)
        {
            // previous indentation
            c.removeSelectedText();
            QRegularExpression indent_regex("(^\\s*)(?=[^\\s\\r\\n]+)");
            QTextCursor prev_c = e->document()->find(indent_regex, c, QTextDocument::FindBackward);

            if (!prev_c.isNull())
            {
                // all leading characters are \s => shift start search point up
                if (prev_c.selectionEnd() >= c.position())
                {
                    QTextCursor upper(e->document());
                    upper.setPosition(prev_c.selectionStart());
                    prev_c = e->document()->find(indent_regex, upper, QTextDocument::FindBackward);
                }

                if (!prev_c.isNull())
                {
                    // remove subsequent \s
                    QTextCursor next_c = e->document()->find(QRegularExpression("(\\s+)(?=\\S+)"), c);
                    if (next_c.selectionStart() == c.selectionStart())
                        next_c.removeSelectedText();
                }
            }
            // insert indentation
            c.insertText("\n" + prev_c.selectedText());
            return true;
        }

        if (keyEvent->key() == Qt::Key_Home && !keyEvent->modifiers().testFlag(Qt::ControlModifier))
        {
            int startOfText = c.block().position();
            QTextCursor notSpace = e->document()->find(QRegularExpression("\\S"), startOfText);
            if (!notSpace.isNull() && notSpace.block() == c.block() && notSpace.position() - 1 > startOfText)
                startOfText = notSpace.position() - 1;
            int nextPos = startOfText;
            if (c.position() <= startOfText && c.position() > c.block().position())
                nextPos = c.block().position();
            else
                nextPos = startOfText;
            c.setPosition(nextPos, keyEvent->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
            e->setTextCursor(c);
            return true;
        }

        if (!c.hasSelection())
            break;
        int start = c.selectionStart();
        int end = c.selectionEnd();
        if (keyEvent->key() == Qt::Key_U)
        {
            if (keyEvent->modifiers().testFlag(Qt::ControlModifier))
            {
                if (keyEvent->modifiers().testFlag(Qt::ShiftModifier))
                    c.insertText(c.selectedText().toLower());
                else
                    c.insertText(c.selectedText().toUpper());
            }
            else break;
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            e->setTextCursor(c);
            return true;
        }
        else if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            // do not indent if selection is within single block
            c.setPosition(end);
            int lastBlock = c.blockNumber();
            c.setPosition(start);
            if (c.blockNumber() == lastBlock)
                break;

            c.movePosition(QTextCursor::StartOfBlock);
            int startOfFirstBlock = c.position();
            c.beginEditBlock();
            bool firstPass = true;
            if (keyEvent->key() == Qt::Key_Backtab)
            {
                int tabSize = SqtSettings::value("tabSize", 3).toInt();
                do
                {
                    if (!firstPass)
                    {
                        if (!c.movePosition(QTextCursor::NextBlock))
                            break;
                    }
                    else
                        firstPass = false;

                    if (c.position() >= end)
                        break;

                    int tab = tabSize;
                    while (tab > 0)
                    {
                        QChar curChar = e->document()->characterAt(c.position());
                        if (QString(" \t").contains(curChar))
                        {
                            c.setPosition(c.position() + 1, QTextCursor::KeepAnchor);
                            c.removeSelectedText();
                            --end;
                            if (start > startOfFirstBlock && c.position() <= start)
                                --start;
                            tab -= (curChar == '\t' ? tabSize : 1);
                        }
                        else
                            break;
                    }
                    /*if (tabSize < 0)
                    {
                        c.insertText(QString(' ').repeated(-tabSize));
                        end -= tabSize;
                    }*/
                }
                while (true);
            }
            else
            {
                do
                {
                    if (!firstPass)
                    {
                        if (!c.movePosition(QTextCursor::NextBlock))
                            break;
                    }
                    else
                        firstPass = false;
                    if (c.position() >= end)
                        break;
                    if (c.position() <= start)
                        ++start;
                    c.insertText("\t");
                    ++end;
                }
                while (true);
            }
            c.endEditBlock();
            c.setPosition(start < 0 ? 0 : start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            e->setTextCursor(c);
            return true;
        }
        break;
    }
    default:
        break;
    }
    return QObject::eventFilter(object, event);
}
