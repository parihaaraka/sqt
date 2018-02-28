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

QueryWidget::QueryWidget(QWidget *parent) : QueryWidget(nullptr, parent)
{
}

QueryWidget::QueryWidget(DbConnection *connection, QWidget *parent) :
    QSplitter(parent), _editor(nullptr), _connection(connection)
{
    _messages = nullptr;
    _highlighter = nullptr;

    _resultMenu = new QMenu(this);
    _actionCopy = new QAction(tr("Copy CSV"), this);
    _actionCopy->setShortcuts(QKeySequence::Copy);
    _resultMenu->addAction(_actionCopy);
    connect(_actionCopy, &QAction::triggered, this, &QueryWidget::onActionCopyTriggered);

    /*_actionCopyHTML = new QAction(tr("Copy html"), this);
    _resultMenu->addAction(_actionCopyHTML);
    connect(_actionCopyHTML, &QAction::triggered, this, &QueryWidget::on_actionCopy_triggered);*/

    _editorLayout = new QVBoxLayout(this);
    _editorLayout->setSpacing(0);
    _editorLayout->setMargin(0);
    QWidget *w = new QWidget(this);
    w->setLayout(_editorLayout);

    addWidget(w);

    if (_connection)
    {
        QTabWidget *res = new QTabWidget(this);
        res->setDocumentMode(true);
        _resSplitter = new QSplitter(res);
        _messages = new QPlainTextEdit(res);
        _messages->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        res->addTab(_messages, tr("messages"));
        addWidget(res);
        setSizes(QList<int>() << 1 << 0);
        setOrientation(Qt::Vertical);

        connect(_connection.get(), &DbConnection::fetched, this, &QueryWidget::fetched);
        connect(_connection.get(), &DbConnection::message, this, &QueryWidget::onMessage);
        connect(_connection.get(), &DbConnection::error, this, &QueryWidget::onError);
        _connection->open();
    }

    highlight(_connection);
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

template<class T>
T* initEditor(QWidget **textEdit, QueryWidget *parent)
{
    T *editor = qobject_cast<T*>(*textEdit);
    if (editor)
        return editor;

    editor = new T(parent);
    QFont font("Consolas, monospace, Menlo, Lucida Console, Liberation Mono, DejaVu Sans Mono, Bitstream Vera Sans Mono, Courier New, serif");
    font.setStyleHint(QFont::TypeWriter);
    font.setPointSize(9);
    editor->setFont(font);
    editor->setTabStopWidth(QFontMetrics(font).averageCharWidth() * 3);
    editor->setWordWrapMode(QTextOption::NoWrap);
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
    //connect(w->queryEditor(), &QPlainTextEdit::selectionChanged, this, &MainWindow::refreshCursorInfo);

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
    QPlainTextEdit *editor = initEditor<QPlainTextEdit>(&_editor, this);
    editor->setPlainText(text);
    //editor->document()->setModified(false);
}

void QueryWidget::setHtml(const QString &html)
{
    QTextEdit *editor = initEditor<QTextEdit>(&_editor, this);
    editor->setHtml(html);
    //editor->document()->setModified(false);
}

void QueryWidget::onMessage(const QString &text)
{
    _messages->appendPlainText(text);
    if (!text.isEmpty() && widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);
}

void QueryWidget::onError(const QString &err)
{
    _messages->appendHtml(QString("<font color='red'>%1</font>").arg(err.toHtmlEscaped()));
    if (!err.isEmpty() && widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);
}

void QueryWidget::fetched(DataTable *table)
{
    if (widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);
    QTabWidget *res_tw = qobject_cast<QTabWidget*>(widget(1));
    if (res_tw->count() == 1)
    {
        res_tw->insertTab(0, _resSplitter, tr("resultsets"));
        res_tw->setCurrentIndex(0);
    }

    //DbConnection *con = qobject_cast<DbConnection*>(sender());
    QString tname = QString::number(std::intptr_t(table));
    QTableView *tv = nullptr;
    TableModel *m = nullptr;
    if (_resSplitter->count())
        tv = qobject_cast<QTableView*>(_resSplitter->widget(_resSplitter->count() - 1));
    if (!tv || tv->objectName() != tname)
    {
        tv = new QTableView(_resSplitter);
        tv->setObjectName(tname);
        tv->verticalHeader()->setDefaultSectionSize(tv->verticalHeader()->minimumSectionSize());
        tv->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(tv, &QTableView::customContextMenuRequested, this, &QueryWidget::onCustomGridContextMenuRequested);
        tv->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tv->addAction(_actionCopy);

        m = new TableModel(_resSplitter);
        _tables.append(m);
        tv->setModel(m);
        _resSplitter->addWidget(tv);
        m->take(table);
        tv->resizeColumnsToContents();
    }
    else
    {
        m = qobject_cast<TableModel*>(tv->model());
        m->take(table);
    }
    QCoreApplication::processEvents();
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
            res_tw->removeTab(0);
            for (int i = _resSplitter->count() - 1; i >= 0; --i)
            {
                delete _resSplitter->widget(i);
            }
        }
    }
    qDeleteAll(_tables);
    _tables.clear();
}

void QueryWidget::onCustomGridContextMenuRequested(const QPoint &pos)
{
    QTableView *tv = qobject_cast<QTableView*>(sender());
    if (!tv || !tv->model()->rowCount())
        return;

    _resultMenu->exec(tv->mapToGlobal(pos));
}

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
    foreach(QModelIndex cur, indexes)
    {
        //QMetaType::Type t = (QMetaType::Type)m->data(cur).type();
        QString val = m->data(cur).toString();
        if (!prev.isValid()) ;
        else if (cur.row() != prev.row()) result.append(isHTML ? QString("</tr><tr>") : QString(QChar::CarriageReturn));
        else if (!isHTML) result.append(',');

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

void QueryWidget::onCursorPositionChanged()
{
    QList<QTextEdit::ExtraSelection> left_bracket;
    QList<QTextEdit::ExtraSelection> right_bracket;
    setExtraSelections(left_bracket);
    QTextCursor cursor = textCursor();
    if (cursor.selectedText().length() > 1 || !_highlighter || !_highlighter->document())
        return;

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
        right_bracket = matchBracket(c, left_bracket.empty() ? 100 : 115);
    }
    setExtraSelections(right_bracket + left_bracket);
}

bool QueryWidget::isEnveloped(const QTextCursor &c)
{
    int pos = c.positionInBlock();
    if (c.hasSelection() && c.anchor() < c.position())
        pos -= 1;
    foreach (QTextLayout::FormatRange r, c.block().layout()->formats())
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
    Q_UNUSED(object)
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
            e->setCursorWidth(e->overwriteMode() ? e->cursorWidth() * 3 : e->cursorWidth() / 3);
            return true;
        }

        QTextCursor c = e->textCursor();
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

                    int tabSize = 3;  // TODO: move to settings
                    while (tabSize > 0)
                    {
                        QChar curChar = e->document()->characterAt(c.position());
                        if (QString(" \t").contains(curChar))
                        {
                            c.setPosition(c.position() + 1, QTextCursor::KeepAnchor);
                            c.removeSelectedText();
                            --end;
                            if (start > startOfFirstBlock && c.position() <= start)
                                --start;
                            tabSize -= (curChar == '\t' ? 3 : 1);
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
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::Hide:
        // change environment on tab change ?
        if (MainWindow *mainWindow = qobject_cast<MainWindow*>(this->window()))
        {
            mainWindow->refreshActions();
            mainWindow->refreshContextInfo();
            mainWindow->refreshCursorInfo();
        }
        break;

    default:
        break;
    }
    return false;
}
