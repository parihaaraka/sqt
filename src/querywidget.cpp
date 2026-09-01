#include "querywidget.h"
#include <QCoreApplication>
#include <QTabWidget>
#include <QApplication>
#include "misc.h"
#include "settings.h"
#include "sqlsyntaxhighlighter.h"
#include "dbconnectionfactory.h"
#include "dbconnection.h"
#include <QTableView>
#include <QHeaderView>
#include "styling.h"
#include "tablemodel.h"
#include <QFile>
#include <QMessageBox>
#include <QTextStream>
#include <QMenu>
#include <QVBoxLayout>
#include "findandreplacepanel.h"
#include <QKeyEvent>
#include <memory>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "dbobjectsmodel.h"
#include "mainwindow.h"
#include "scripting.h"
#include "codeeditor.h"
#include <QTextBrowser>
#include <QScrollBar>
#include <QCompleter>
#include <QPointer>
#include <QToolTip>
#include <QTimer>
#include "sqlparser.h"
#include "sqllexer.h"
#include "datatable.h"
#include "timechart.h"
#include <QStatusBar>
#include "textcodec.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>

QueryWidget::QueryWidget(QWidget *parent) : QueryWidget(nullptr, parent)
{
}

QueryWidget::QueryWidget(DbConnection *connection, QWidget *parent) :
    QSplitter(parent), _editor(nullptr)
{
    _timer = new QTimer(this);
    _timer->setTimerType(Qt::PreciseTimer);
    _messages = nullptr;
    _highlighter = nullptr;
    _editorLayout = new QVBoxLayout(this);
    _editorLayout->setSpacing(0);
    _editorLayout->setContentsMargins(0, 0, 0, 0);
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
        w->setParent(nullptr);
    }
    if (_highlighter)
    {
        _highlighter->setDocument(nullptr);
        delete _highlighter;
        _highlighter = nullptr;
    }
}

bool QueryWidget::openFile(const QString &fileName, const QString &encoding)
{
    if (TextCodec::canonicalName(encoding).isEmpty())
    {
        onError(tr("Unknown encoding: %1").arg(encoding));
        return false;
    }

    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        onError(tr("Unable to open %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    const QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError)
    {
        onError(tr("Unable to read %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    f.close();

    _fn = fileName;
    _encoding = encoding;

    // A unicode BOM wins over the encoding asked for, which is what the
    // QTextStream behind this used to do (autoDetectUnicode is on by default).
    // The choice is not remembered: a save still writes _encoding.
    const QString bom = TextCodec::bomEncoding(data);
    bool ok = false;
    const QString text = TextCodec::decode(data, bom.isEmpty() ? _encoding : bom, &ok);
    if (!ok)
        onMessage(tr("%1 is not valid %2; the bad bytes are shown as U+FFFD")
                  .arg(fileName, bom.isEmpty() ? _encoding : bom));

    if (data.size() > 1024 * 1024 * 5) // do not highlight > 5mb scripts
        dehighlight();
    else
        highlight();
    setPlainText(text);
    document()->setModified(false);
    return true;
}


bool QueryWidget::saveFile(const QString &fileName, const QString &encoding)
{
    // Checked before the file is opened: opening it for writing truncates it,
    // so bailing out afterwards used to destroy the very text being saved.
    const QString enc = (encoding.isEmpty() ? _encoding : encoding);
    if (TextCodec::canonicalName(enc).isEmpty())
    {
        onError(tr("Unknown encoding: %1").arg(enc));
        return false;
    }

    QString text;
    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
    {
        bool keepTrailingSpaces = SqtSettings::value("keepTrailingSpaces", false).toBool();
        // remove trailing space characters without regexp
        // * but save exactly 3 whitespaces! It's about bitbucket's markdown :(
        QTextDocument *doc = plain->document();
        for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next())
        {
            if (it != doc->begin())
                text += '\n';

            QString line = it.text();

            if (!keepTrailingSpaces)
            {
                qsizetype len = line.size();
                int whitespace_count = 0;
                int any_space_count = 0;
                // the counter is the loop variable here: a line made of spaces only
                // used to run the index past the front of the line
                while (any_space_count < len)
                {
                    QChar c = line.at(len - any_space_count - 1);
                    if (!c.isSpace())
                        break;
                    ++any_space_count;
                    if (c == ' ')
                        ++whitespace_count;
                }
                if (whitespace_count != 3 || whitespace_count != any_space_count)
                    len -= any_space_count;
                text += line.mid(0, len);
            }
            else
            {
                text += line;
            }
        }
    }
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        text = rich->toPlainText();

    bool ok = false;
    const QByteArray data = TextCodec::encode(text, enc, &ok);

    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly))
    {
        onError(tr("Unable to save %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    if (f.write(data) != data.size() || !f.flush())
    {
        onError(tr("Unable to save %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    f.close();

    _fn = fileName;
    _encoding = enc;
    // The file is written either way - refusing to save would be worse - but
    // the loss is silent otherwise, and '?' is not what the author typed.
    if (!ok)
        onMessage(tr("Some characters are not available in %1 and were saved as '?'").arg(enc));
    return true;
}


void QueryWidget::setDbConnection(DbConnection *connection)
{
    bool dbConnectionChanged = false;
    if (_connection.get() != connection)
    {
        dbConnectionChanged = true;
        _connection.reset(connection);
    }

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

        // Arrives just before the Inactive state below, and a queued connection
        // preserves that order for one sender/receiver pair - so the flag is
        // always set by the time the state change is handled.
        connect(connection, &DbConnection::outcomeUnknown, this, [this]() {
            _outcomeUnknown = true;
        }, Qt::QueuedConnection);

        connect(connection, &DbConnection::queryStateChanged, this, [this](QueryState queryState) {
            // actual query execution time before post-processing
            if (queryState == QueryState::Inactive)
            {
                // "done" would mean the server has answered; when it never did,
                // say so instead, or the natural retry may run the query twice
                if (_outcomeUnknown)
                    onMessage(tr("%1: interrupted after %2, outcome unknown")
                              .arg(QTime::currentTime().toString("HH:mm:ss"), _connection->elapsed()));
                else
                    onMessage(tr("%1: done in %2").arg(QTime::currentTime().toString("HH:mm:ss"), _connection->elapsed()));
                _outcomeUnknown = false;

                // The last word of the run, so the flag goes down after it and
                // not before: everything the connection has emitted up to this
                // point is the query's own output. Queued deliveries from one
                // sender to one receiver keep their order, hence the guarantee.
                _queryActive = false;
            }

            if (MainWindow *mainWindow = qobject_cast<MainWindow*>(window()))
                mainWindow->queryStateChanged(this, queryState);
        }, Qt::QueuedConnection);

        connect(connection, &DbConnection::queryFinished, this, [this]() {
            // print all resultsets structure ready to be used in 'create function returning table(...)'
            QColor resultsetStructureColor = _messages->palette().text().color();
            resultsetStructureColor.setAlphaF(0.6f);
            // A copy of the list, not the list itself: it is still open to
            // appends from the connection's own thread (a notice arriving on
            // the link lands there through libpq's callback), and iterating it
            // in place would race with a reallocation. The tables themselves
            // stay owned by the connection until the next run clears them.
            const QList<DataTable*> resultsets = _connection->resultsetsSnapshot();
            for (const auto res: resultsets)
            {
                if (!res->columnCount())
                    continue;

                // initially created models (while fetching data) have not acquired column types yet
                // (unlike the connection's own tables)
                TableModel *m = _resSplitter->findChild<TableModel*>("m" + QString::number(std::intptr_t(res)));
                if (m)
                    _connection->clarifyTableStructure(*m->table());

                QString structure, fieldsList;
                for (int i = 0; i < res->columnCount(); ++i)
                {
                    auto &c = res->getColumn(i);
                    QString typeDescr = c.typeName();

                    if (i)
                    {
                        structure += ", ";
                        fieldsList += ", ";
                    }

                    QString tmp = _connection->escapeIdentifier(c.name());
                    if (    c.name().length() == tmp.length() - 2 && // quotes only appendes
                            tmp == tmp.toLower() &&  // originally lowercase
                            _highlighter && !_highlighter->isKeyword(c.name())) // not a keyword
                        tmp = c.name(); // take original name witout quotes

                    structure += tmp + ' ' + typeDescr;
                    fieldsList += tmp;
                }

                if (!isTimerActive())
                    log('(' + structure + ")\nselect " + fieldsList, resultsetStructureColor);
            }

            // scroll down and left
            auto cursor = _messages->textCursor();
            cursor.movePosition(QTextCursor::End);
            cursor.movePosition(QTextCursor::StartOfLine);
            _messages->setTextCursor(cursor);
        }, Qt::QueuedConnection);

        connection->open();
    }

    if (dbConnectionChanged)
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

void QueryWidget::highlight(std::shared_ptr<DbConnection> con, bool force)
{
    if (force || _connection != con || !_highlighter)
    {
        if (con)
            _connection = con;

        QJsonDocument settings;
        if (_connection)
        {
            try
            {
                // a bundle without a palette of its own is not an error
                if (const QString hl = Scripting::dbmsFile(_connection.get(), "hl.conf"); !hl.isEmpty())
                    settings = readJsonFile(hl);
            }
            catch (const QString &err)
            {
                emit error(err);
            }
        }

        if (_highlighter)
        {
            _highlighter->setDocument(nullptr);
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
        _highlighter->setDocument(nullptr);
}

void QueryWidget::rehighlight()
{
    if (_highlighter && _highlighter->document())
        _highlighter->rehighlight();
}

bool QueryWidget::is_sql_hl(QPlainTextEdit *ed)
{
    return _highlighter
            && _highlighter->document()
            && _highlighter->document() == (ed ? ed->document() : nullptr);
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
    // Whatever file the text belonged to, it is gone with the text.
    _shownFile.clear();
    _shownFileRoot.clear();
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

void QueryWidget::setMatchHighlight(const QTextCursor &range, const QColor &color)
{
    // Only a CodeEditor carries the mark; the html browser used for object
    // content has no such notion, and a file preview is always the former.
    if (CodeEditor *ed = qobject_cast<CodeEditor*>(_editor))
        ed->setMatchHighlight(range, color);
}

void QueryWidget::clearMatchHighlight()
{
    if (CodeEditor *ed = qobject_cast<CodeEditor*>(_editor))
        ed->clearMatchHighlight();
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

QPair<int, int> QueryWidget::currentStatementBounds()
{
    if (!_connection)
        return {-1, -1};
    auto lexer = SqlLexer::sharedFor(_connection.get());
    if (!lexer)
        return {-1, -1};
    return lexer->statementBounds(toPlainText(), textCursor().position());
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
    editor->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard | Qt::LinksAccessibleByMouse);

    //_editor->setContextMenuPolicy(Qt::CustomContextMenu);
    //connect(_editor, &QTextEdit::customContextMenuRequested, this, &QueryWidget::on_customEditorContextMenuRequested);
    QObject::connect(editor->document(), &QTextDocument::contentsChanged, parent, &QueryWidget::sqlChanged);

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
    // A new text has arrived; unless the caller says otherwise (setShownFile,
    // right after this) it belongs to no file - a tree node's script, a query
    // result, the next preview.
    _shownFile.clear();
    _shownFileRoot.clear();
    CodeEditor *editor = initEditor<CodeEditor>(&_editor, this);
    connect(editor, &CodeEditor::completerRequest, this, &QueryWidget::onCompleterRequest, Qt::UniqueConnection);
    connect(editor, &CodeEditor::scriptObjectRequest, this, &QueryWidget::onScriptObjectRequest, Qt::UniqueConnection);
    connect(editor, &CodeEditor::executeStatementRequest, this, &QueryWidget::onExecuteStatementRequest, Qt::UniqueConnection);
    connect(editor, &CodeEditor::selectStatementRequest, this, &QueryWidget::onSelectStatementRequest, Qt::UniqueConnection);
    connect(editor, &CodeEditor::copyCodeLocationRequest, this, &QueryWidget::onCopyCodeLocationRequest, Qt::UniqueConnection);
    connect(editor, &CodeEditor::contextMenuRequest, this, &QueryWidget::onEditorContextMenu, Qt::UniqueConnection);
    editor->setPlainText(text);
}

void QueryWidget::setShownFile(const QString &fileName, const QString &root)
{
    _shownFile = fileName;
    _shownFileRoot = root;
}

void QueryWidget::setHtml(const QString &html)
{
    _shownFile.clear();
    _shownFileRoot.clear();
    //QTextEdit *editor = initEditor<QTextEdit>(&_editor, this);
    QTextBrowser *editor = initEditor<QTextBrowser>(&_editor, this);
    editor->setOpenExternalLinks(true);
    editor->setHtml(html);
}

void QueryWidget::setQuerySettings(QJsonObject &querySettings)
{
    _querySettings.swap(querySettings);
}

bool QueryWidget::isTimerActive() const
{
    return _timer->isActive();
}

void QueryWidget::stopTimer()
{
    _timer->stop();
}

void QueryWidget::executeOnTimer(const QString &query, int interval)
{
    _timer->disconnect();
    connect(_timer, &QTimer::timeout, this, [this, query] {
        if (_connection->queryState() == QueryState::Inactive)
            execute(query);
    });
    execute(query);
    _timer->start(interval);
}

void QueryWidget::execute(const QString &query)
{
    if (!_connection)
        return;
    // Raised before the connection can report anything at all, including a
    // synchronous refusal to start.
    _queryActive = true;
    if (!_connection->executeAsync(query))
    {
        // A refused query reports nothing at all, so there will be no
        // Inactive state to lower the flag. Polling queryState() instead
        // would race: Running is set by the worker thread, which has not
        // necessarily started yet.
        _queryActive = false;
    }
}

void QueryWidget::log(const QString &text, QColor color)
{
    // widgets created without a connection (the object tree preview pane)
    // have no messages pane
    if (!_messages)
        return;

    QTextCharFormat fmt = _messages->currentCharFormat();
    fmt.setForeground(QBrush(color));
    _messages->mergeCurrentCharFormat(fmt);

    _messages->appendPlainText(text.trimmed());
    if (!text.isEmpty() && widget(1)->height() == 0)
        setSizes(QList<int>() << 400 << 100);
}

void QueryWidget::note(const QString &text)
{
    // The pane must show one run's output only, so an aside about this very run
    // starts it: whatever the previous one left behind would read as part of
    // the answer to what the user has just asked.
    clearResult();
    // Same colour as everything else the run has to say: this *is* its result,
    // the whole point being that a run which produces nothing at all otherwise
    // looks like the application having failed to react.
    const QPalette defaultPalette;
    log(text, defaultPalette.color(QPalette::Text));
}

void QueryWidget::status(const QString &text)
{
    // A passing remark about something that is not a query run (no statement to
    // select, no object to script): transient, and never in the messages pane,
    // where it would be read as the output of the query displayed there. The
    // preview pane has no messages pane at all, and no modal popup either way.
    if (QMainWindow *mw = qobject_cast<QMainWindow*>(window()))
    {
        if (mw->statusBar())
        {
            mw->statusBar()->showMessage(text, 1000 * 5);
            return;
        }
    }
    // no status bar to notify through
    log(text, QPalette().color(QPalette::Text));
}

QString QueryWidget::codeLocation() const
{
    // Only a code editor has lines to name; the html browser used for object
    // content has no such notion, and neither has an empty widget.
    CodeEditor *ed = qobject_cast<CodeEditor*>(_editor);
    if (!ed)
        return QString();

    // Which file the text on screen belongs to. A tab has its own (_fn), the
    // content pane is told what it is previewing (setShownFile). Everything
    // else - a scripted object, a tab never saved - has no file, and pointing
    // an agent at a path that does not exist is worse than offering nothing.
    QString fileName = (_fn.isEmpty() ? _shownFile : _fn);
    if (fileName.isEmpty())
        return QString();

    const QFileInfo fi(fileName);
    if (!fi.isFile())
        return QString();

    // A file search hit is reported relative to the folder that was searched:
    // its results are read as one project, and that root is the directory an
    // agent will be started in. Only when the file really lies inside it -
    // otherwise the '..' hops would be less useful than the full path.
    QString path = fi.absoluteFilePath();
    if (_fn.isEmpty() && !_shownFileRoot.isEmpty())
    {
        const QString root = QFileInfo(_shownFileRoot).absoluteFilePath();
        const QString rel = QDir(root).relativeFilePath(path);
        if (!rel.startsWith(".."))
            path = rel;
    }
    // Native separators: the string is going into a prompt a human reads, and
    // on windows Qt's internal '/' form is not what anything else there shows.
    path = QDir::toNativeSeparators(path);

    const QPair<int, int> lines = ed->selectedLineSpan();
    return (lines.first == lines.second ?
                QString("%1:%2").arg(path).arg(lines.first) :
                QString("%1:%2-%3").arg(path).arg(lines.first).arg(lines.second));
}

QCompleter* QueryWidget::completer()
{
    // One completer shared by every query widget. It must *not* be parented to
    // the application object, and a static instance is no better: both outlive
    // ~QApplication. Children of qApp are deleted by ~QObject, which runs after
    // the bodies of ~QApplication/~QGuiApplication have already destroyed the
    // platform integration, the style and the main thread's event dispatcher;
    // a static goes even later, after main() has returned. The completer owns a
    // top-level QListView (its popup), and destroying a widget that late writes
    // into freed memory. Qt says as much on the way out ("QBasicTimer can only
    // be used with threads started with QThread" - what it prints when there is
    // no event dispatcher at all), and the next free() aborts with "corrupted
    // double-linked list", usually while some unrelated static container is
    // being destroyed. Without a single Ctrl+Space the completer is never built
    // and the very same exit is clean, which is what makes the abort look like
    // a bug in whatever container happened to be freed first.
    //
    // Hence it is deleted on aboutToQuit, while the application is still whole,
    // and a request arriving after that gets nothing rather than a fresh popup
    // that nobody would destroy in time.
    static QPointer<QCompleter> instance;
    static bool shuttingDown = false;
    if (!instance && !shuttingDown && !QCoreApplication::closingDown())
    {
        QCompleter *c = new QCompleter;
        instance = c;
        // qApp as the context object keeps the connection alive no longer than
        // the sender. A plain delete, not deleteLater(): deferred deletion needs
        // an event loop, and there is none left to run it by then.
        connect(qApp, &QCoreApplication::aboutToQuit, qApp, [c]() {
            shuttingDown = true;
            delete c;
        });
        connect(c->popup()->selectionModel(), &QItemSelectionModel::currentChanged, c,
                [c](const QModelIndex &current, const QModelIndex &) {
            if (current.isValid())
            {
                auto popup = c->popup();
                auto m = current.model();
                if (!popup->isVisible() || m->columnCount() < 2)
                    return;

                QJsonDocument doc = QJsonDocument::fromJson(m->index(current.row(), 1).data().toString().toUtf8());
                QJsonArray arr;
                if (doc.isObject())
                    arr += doc.object();
                else if (doc.isArray())
                    arr = doc.array();

                QString tooltip;
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
                for (const auto &i: qAsConst(arr))
#else
                for (const auto &i: std::as_const(arr))
#endif
                {
                    if (!i.isObject())
                        continue;

                    auto obj = i.toObject();
                    QString name = obj["n"].toString();
                    QString info = obj["d"].toString();
                    if (name.isEmpty() && info.isEmpty())
                        continue;

                    if (!tooltip.isEmpty())
                        tooltip += "<br/><br/>";

                    tooltip +=
                            (name.isEmpty() ?
                                "" :
                                "<b>" + name.toHtmlEscaped().replace(' ', "&nbsp;") + "</b>")
                            + (info.isEmpty() ?
                                "" :
                                (name.isEmpty() ? "" : "<br/>") + info.toHtmlEscaped());
                }

                if (!tooltip.isEmpty())
                {
                    QToolTip::showText(popup->mapToGlobal(popup->rect().bottomLeft()), tooltip);
                    return;
                }
            }
            QToolTip::hideText();
        });
    }
    return instance;
}

// The shared log needs to know which tab is talking; the tab's own messages
// pane obviously does not, so the prefix is added here and nowhere else.
QString QueryWidget::logMessage(const QString &text) const
{
    return _title.isEmpty() ?
                text.trimmed() :
                QString("`%1` tab:\n%2").arg(_title, text.trimmed());
}

void QueryWidget::onMessage(const QString &text)
{
    // The messages pane holds the result of a query run and nothing else.
    // Anything the connection says on its own (a link established or lost, a
    // notification received while idle) goes to the log.
    if (!_queryActive)
    {
        emit message(logMessage(text));
        return;
    }

    if (!isTimerActive())
    {
        const QPalette defaultPalette;
        log(text, defaultPalette.color(QPalette::Text));
    }
}

void QueryWidget::onError(const QString &text)
{
    // Same rule as above: a failure of the running query is its result and
    // stays in the pane; a failure of anything else (the connection itself,
    // a file, an F4 request) is not, and would be read as the result of the
    // query still displayed there.
    if (!_queryActive)
    {
        emit error(logMessage(text));
        return;
    }
    log(text, QColor::fromRgba(0xE0FF4040)); // NOLINT
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

    if (_querySettings.contains("charts"))
    {
        QVector<TimeChart*> chartWidgets;
        if (!_resSplitter->count())
        {
            // create charts
            const QJsonArray jCharts = _querySettings["charts"].toArray();
            for (auto g: jCharts)
            {
                auto gObj = g.toObject();
                TimeChart *chart = new TimeChart(_resSplitter);
                chartWidgets.append(chart);
                chart->setObjectName(gObj["name"].toString());
                chart->setXSourceField(gObj["x"].toString());
                auto difYObj = gObj["agg_y"].toObject();
                for (QString &k: difYObj.keys())
                    chart->createPath(k, QColor(difYObj[k].toString()), true);
                difYObj = gObj["y"].toObject();
                for (QString &k: difYObj.keys())
                    chart->createPath(k, QColor(difYObj[k].toString()), false);

                _resSplitter->addWidget(chart);
            }
        }
        else
        {
            // the splitter may still hold table views left by a query without
            // charts, and a failed cast would put a null into the list
            for (int i = 0; i < _resSplitter->count(); ++i)
                if (TimeChart *chart = qobject_cast<TimeChart*>(_resSplitter->widget(i)))
                    chartWidgets.append(chart);
        }

        // The rows are taken out under the producer's own lock and consumed from
        // a local table afterwards. Walking the live one was a race: the worker
        // emits fetched() every FETCH_COUNT_NOTIFY rows from inside its append
        // loop and keeps appending (each append under table->mutex), while this
        // branch read rowCount()/getRow() and then clearRows() - which frees the
        // row objects and reallocates the vector - with no lock at all. The
        // non-chart branch below already does this correctly, through
        // TableModel::take().
        DataTable rows;
        {
            QMutexLocker lk(&table->mutex);
            rows.takeRows(table);
        }

        for (auto c: chartWidgets)
        {
            QString xSourceField = c->xSourceField();
            for (auto &n: c->pathNames())
            {
                int ind = rows.getColumnOrd(n);
                if (ind != -1)
                {
                    int sfInd = -1;
                    if (!xSourceField.isEmpty())
                    {
                        // if x-source field is specified but not found then skip current chart
                        // (path by path, because I'm too lazy to skip paths too)
                        sfInd = rows.getColumnOrd(xSourceField);
                        if (sfInd == -1)
                            continue;
                    }
                    for (int r = 0; r < rows.rowCount(); ++r)
                    {
                        bool ok;
                        qreal v = rows.getRow(r)[n].toDouble(&ok);
                        if (ok)
                        {
                            if (sfInd == -1)
                                c->appendValue(n, v, QDateTime::currentDateTime());
                            else
                            {
                                QString tsStr = rows.getRow(r)[sfInd].toString();
                                // TODO implement pg/odbc-specific time/datetime values conversion
                                QDateTime ts = QDateTime::fromString(tsStr, Qt::ISODateWithMs);
                                if (ts.isValid())
                                    c->appendValue(n, v, ts);
                            }
                        }
                    }
                }
            }
            c->applyNewValues();
        }

    }
    else
    {
        if (_resSplitter->count())
            tv = qobject_cast<QTableView*>(_resSplitter->widget(_resSplitter->count() - 1));

        if (!tv || tv->objectName() != tname)
        {
            tv = new QTableView(_resSplitter);
            // Matches what the FontChange handler in AppEventHandler sets later -
            // see comfortableRowHeight() - so a freshly created grid starts out
            // with the same row height it would otherwise only get after the
            // next font or stylesheet change, rather than the tighter
            // lineSpacing() a plain single-line box would need.
            tv->verticalHeader()->setDefaultSectionSize(comfortableRowHeight(tv));
            // Painted by hand instead - see paintPixelPerfectGrid() and
            // AppEventHandler's QEvent::Paint case for why and where.
            tv->setShowGrid(false);
            tv->horizontalHeader()->viewport()->setMouseTracking(true);
            tv->setObjectName(tname);

            //tv->setContextMenuPolicy(Qt::CustomContextMenu);
            //connect(tv, &QTableView::customContextMenuRequested, this, &QueryWidget::onCustomGridContextMenuRequested);
            //tv->setSelectionMode(QAbstractItemView::ContiguousSelection);
            //tv->addAction(_actionCopy);

            // the view owns the model: they are created and dropped together,
            // and findChild() below still reaches it through the view
            m = new TableModel(tv);
            m->setObjectName("m" + tname);
            tv->setModel(m);
            _resSplitter->addWidget(tv);
            m->take(table);
            // prevent autoresize overhead when big resultset is fetched at once
            tv->horizontalHeader()->setResizeContentsPrecision(20);
            tv->resizeColumnsToContents();
            // A row height alone (see comfortableRowHeight() above) only keeps
            // the *horizontal* grid lines crisp at a fractional display scale;
            // the vertical ones need every column's own width snapped too.
            keepColumnsSnappedToDevicePixels(tv);
        }
        else
        {
            m = qobject_cast<TableModel*>(tv->model());
            if (m)
                m->take(table);
        }
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
        // The resultsets tab is inserted along with the very first view and
        // holds every one of them, so asking for the tab asks exactly the right
        // question: whether there is anything to drop. Counting tabs instead
        // would only work while there happens to be a second one.
        const int resTabIndex = (res_tw && _resSplitter ? res_tw->indexOf(_resSplitter) : -1);
        if (resTabIndex >= 0)
        {
            // remove resultsets tab, _resSplitter stays alive
            res_tw->removeTab(resTabIndex);
            // delete QTableView widgets (current shown resultsets)
            // (it looks like simple delete works ok instead of setParent(nullptr) and deleteLater())
            for (int i = _resSplitter->count() - 1; i >= 0; --i)
                delete _resSplitter->widget(i);
        }
    }
}

void QueryWidget::onCompleterRequest()
{
    // TODO  cache and much, much more :)

    CodeEditor *ed = qobject_cast<CodeEditor*>(sender());
    /*
     Possible positions on current word:
     cur_word (standalone)
        Schema/table/function and so on.
        Do not gather all columns of all available tables (for a while),
        but it may be useful in all parts of a query except FROM.
     p0.cur_word
        p0
            SELECT:     schema/alias/cte/table (fall through records and composite values)
            FROM/JOIN:  schema/cte/table (alias within parentheses only)
            join ON:    schema(to be followed by table.column)/alias/cte/table
        cur_word
            p0 is table/view ? columns of p0 : schema objects
     p0.p1.cur_word
        p0: schema only
        p1: table
        cur_word: column
    */

    if (!_connection || ed->textCursor().hasSelection())
        return;

    // Temporary parser of just the current identifier.
    // Last word is the word under cursor (may me empty).

    int pos = ed->textCursor().positionInBlock();
    QString content = ed->textCursor().block().text();
    QStringList words;
    QString word;
    bool identifierStarted = false;
    auto pushWord = [&words, &identifierStarted](QString &word) ->bool {
        if (!word.isEmpty())
        {
            if (!identifierStarted && word[0].isDigit())
                return false;
            words.push_front(word);
            word.clear();
        }
        else if (identifierStarted)
            return false;
        else if (words.isEmpty()) // always have current word as a last list item (may be empty)
        {
            words.push_front(word);
            word.clear();
        }
        return true;
    };

    QChar prevChar;
    while (pos)
    {
        QChar c = content[--pos];
        if (c == '"')
        {
            if (!identifierStarted && prevChar != '.')
                return;
            pushWord(word);
            identifierStarted = !identifierStarted;
        }
        else if (identifierStarted || c.isLetterOrNumber() || c == '_')
        {
            word = c + word;
            if (!pos && (identifierStarted || prevChar == '"' || !pushWord(word)))
                return;
        }
        else
        {
            if (!pushWord(word))
                return;
            if (c != '.')
                break;
        }
        prevChar = c;
    }

    if (words.isEmpty() || words.count() > 3 || !_connection || !_connection->open())
        return;

    std::unique_ptr<Scripting::CppConductor> c;
    auto exec = [this, &c, &words](const QString &objectType)
    {
        auto env = [&words, &objectType](const QString &macro) -> QVariant
        {
            // last word is completion prefix only
            if (words.count() == 1)
                return QVariant();

            if (macro == "schema.name")
                return words.count() > 2 || objectType == "objects" ? words[0] : QVariant();

            if (macro == "table.name")
                return words.count() > 2 ? words[1] : words[0];

            return QVariant();
        };

        // The editor's own connection is preferred, as only it knows the current
        // search_path. A clone is a separate session and does not, so it is used
        // only when the original is unusable: busy, or inside the user's
        // transaction, which the service queries have no business entering.
        if (_connection->queryState() == QueryState::Inactive &&
            !isTimerActive() &&
            _connection->transactionStatus().isEmpty())
        {
            // The connection may be shared with other widgets, so its slots must
            // stay in place - just keep the service queries silent. Errors are
            // thrown by Scripting::execute() rather than signalled, so blocking
            // the signals hides nothing that matters here.
            QSignalBlocker blocker(_connection.get());
            c = Scripting::execute(_connection.get(), Scripting::Context::Autocomplete, objectType, env);
        }
        else
        {
            std::unique_ptr<DbConnection> tmp_cn(_connection->clone());
            c = Scripting::execute(tmp_cn.get(), Scripting::Context::Autocomplete, objectType, env);
        }
    };

    auto cmpl = completer();
    if (!cmpl) // the application is shutting down
        return;
    std::unique_ptr<TableModel> m(new TableModel(cmpl));

    switch (words.count())
    {
    case 1:
        exec("objects");
        break;
    case 2:
    {
        auto expl = SqlParser::explainAlias(words[0], ed->text(), ed->textCursor().position());
        switch (expl.first)
        {
        case SqlParser::AliasSearchStatus::NotParsed:
            return;
        case SqlParser::AliasSearchStatus::NotFound:
            // previous word may be both table and schema, so we should support both of them
            exec("columns");
            if (c && !c->resultsets.isEmpty())
                m->take(c->resultsets.last());
            exec("objects");
            break;
        case SqlParser::AliasSearchStatus::Name:
        {
            QString prefix = words.last();
            words.swap(expl.second);
            words.append(prefix);
            exec("columns");
            break;
        }
        case SqlParser::AliasSearchStatus::Fields:
            // model does not have a view yet, so no need to use model api
            m->table()->addColumn(new DataColumn());
            for (auto &w: expl.second)
                m->table()->addRow()[0] = w;
        }
        break;
    }
    case 3:
        exec("columns");
    }

    if (c && !c->resultsets.isEmpty())
        m->take(c->resultsets.last());

    if (!m->rowCount())
        return;

    ed->setCompleter(cmpl);
    cmpl->setModelSorting(QCompleter::CaseSensitivelySortedModel);
    // setModel() deletes the model previously set as long as that model is a
    // child of the completer - and every model built here is one. So the models
    // of past completions do not pile up, and disposing of the old one by hand
    // is a double free: the deleteLater() that used to stand here posted a
    // DeferredDelete event to an already freed QObject, which then crashed the
    // application far from the scene, inside sendPostedEvents().
    cmpl->setModel(m.release());
    cmpl->setCompletionPrefix(words.last());
    int cmplCount = cmpl->completionCount();
    if (!cmplCount)
        return;
    if (cmplCount == 1)
        emit cmpl->activated(cmpl->currentCompletion());
    else
    {
        QRect cr = ed->cursorRect();
        auto popup = cmpl->popup();
        cr.setWidth(popup->sizeHintForColumn(0)
                    + popup->verticalScrollBar()->sizeHint().width());
        cmpl->complete(cr);
        // Preselect the first row once the popup has settled. The completer is
        // shared by every tab, so by the time this fires the popup may already
        // be hidden, or may be showing another tab's completion over a model of
        // its own - hence the checks instead of a bare chain of dereferences.
        QTimer::singleShot(100, this, [cmpl = QPointer<QCompleter>(cmpl)]() {
            if (!cmpl)
                return;
            QAbstractItemView *popup = cmpl->popup();
            if (!popup || !popup->isVisible())
                return;
            QAbstractItemModel *model = popup->model();
            QItemSelectionModel *selection = popup->selectionModel();
            if (!model || !selection || !model->rowCount())
                return;
            selection->setCurrentIndex(model->index(0, 0),
                                       QItemSelectionModel::SelectCurrent);
        });
    }
}

void QueryWidget::onExecuteStatementRequest()
{
    // A widget without a messages pane cannot show what a query returns (the
    // object tree's preview pane has none, yet it does hold the tree's
    // connection, lent to it by highlight()) - so running one there would send
    // a query whose every notice and error is dropped on the floor by log().
    if (!_messages)
        return;

    if (MainWindow *mainWindow = qobject_cast<MainWindow*>(window()))
        mainWindow->executeQuery(this, true);
}

void QueryWidget::onSelectStatementRequest()
{
    const QPair<int, int> bounds = currentStatementBounds();
    if (bounds.first < 0)
    {
        // The split is not enabled for this dialect, so the editor cannot tell
        // where one statement ends and the next begins. Said out loud, or the
        // key looks broken; the status bar, since selecting is not a query run.
        status(tr("this dbms does not support running a single statement "
                  "(see `statement_split` in hl.conf)"));
        return;
    }

    if (bounds.first == bounds.second)
    {
        // Between statements, or past the last separator: the bounds are valid
        // but enclose nothing but whitespace, which statementBounds() trims.
        status(tr("no statement at the caret"));
        return;
    }

    QTextCursor c = textCursor();
    c.setPosition(bounds.first);
    c.setPosition(bounds.second, QTextCursor::KeepAnchor);
    setTextCursor(c);
}

void QueryWidget::onEditorContextMenu(QMenu *menu)
{
    if (!menu)
        return;

    // The one place where Ctrl+Shift+A - and with it the whole notion of "the
    // statement under the caret" that Ctrl+Return relies on - can be found by
    // someone who does not already know it is there.
    //
    // Ctrl+Return itself is deliberately not offered as an item: with a
    // selection in place it runs the selection, not the statement at the caret,
    // and a right click does not clear the selection - so the entry would
    // sometimes execute something other than its own wording. Selecting has no
    // such stakes, and once the statement is selected, running it is the
    // familiar Execute query.
    //
    // F4 is left out for the same reason: a right click does not move the
    // caret, so an item scripting "the object under the caret" would act on the
    // caret while the user is pointing at another word.
    auto lexer = (_connection ? SqlLexer::sharedFor(_connection.get()) : nullptr);

    menu->addSeparator();
    QAction *select = menu->addAction(tr("Select statement at the caret"));
    // A menu action hides its shortcut by default - hence the explicit request,
    // so that the style renders the sequence in the platform's own notation
    // instead of it being spelled into the text by hand.
    select->setShortcut(QKeySequence("Ctrl+Shift+A"));
    select->setShortcutVisibleInContextMenu(true);
    // Where the split is not available (no hl.conf, or not enabled for this
    // dialect - see SqlLexer::canSplitStatements) there is no statement to speak
    // of. Shown disabled rather than left out: a menu that changes shape from
    // one connection to the next is no way to learn what the editor can do.
    select->setEnabled(lexer && lexer->canSplitStatements());
    connect(select, &QAction::triggered, this, &QueryWidget::onSelectStatementRequest);

    // "Where is this code" - the file and the line(s) on screen, for a prompt to
    // an ai agent. Left out entirely rather than greyed out when there is no
    // file behind the text (a scripted object, an unsaved tab): unlike the
    // commands above it is not a keyboard shortcut worth advertising in a state
    // where it can never work, and an item saying "copy the path" with no path
    // to copy invites the question of which file it meant.
    if (!codeLocation().isEmpty())
    {
        QAction *copyLocation = menu->addAction(tr("Copy code location"));
        copyLocation->setShortcut(QKeySequence("Ctrl+Shift+C"));
        copyLocation->setShortcutVisibleInContextMenu(true);
        connect(copyLocation, &QAction::triggered, this, &QueryWidget::onCopyCodeLocationRequest);
    }
}

void QueryWidget::onCopyCodeLocationRequest()
{
    const QString location = codeLocation();
    if (location.isEmpty())
    {
        // Reachable by the shortcut alone (the menu hides the item then), and a
        // key that silently does nothing reads as the application being broken.
        status(tr("this text has no file behind it, nothing to point at"));
        return;
    }
    QApplication::clipboard()->setText(location);
    // A confirmation is worth it here: the clipboard gives no feedback of its
    // own, and the point of the command is to paste this elsewhere.
    status(tr("copied: %1").arg(location));
}

void QueryWidget::onScriptObjectRequest()
{
    CodeEditor *ed = qobject_cast<CodeEditor*>(sender());
    if (!ed || !_connection || !_connection->open())
        return;

    // Split the current line into identifier parts to find the one under cursor.
    // Unlike autocompletion we need the whole word, not its left part only.
    struct Part { QString text; bool quoted; int start; int end; bool dotted; };
    QList<Part> parts;
    const QString content = ed->textCursor().block().text();
    bool dotted = false;
    for (int i = 0; i < content.length(); )
    {
        QChar c = content[i];
        if (c == '"')
        {
            int start = i++;
            while (i < content.length() && content[i] != '"')
                ++i;
            if (i == content.length()) // unterminated literal
                break;
            parts.append({content.mid(start + 1, i - start - 1), true, start, ++i, dotted});
            dotted = false;
        }
        else if (c.isLetter() || c == '_')
        {
            int start = i;
            while (i < content.length() && (content[i].isLetterOrNumber() || content[i] == '_'))
                ++i;
            parts.append({content.mid(start, i - start), false, start, i, dotted});
            dotted = false;
        }
        else
        {
            // dot binds the neighbours into a single qualified name
            dotted = (c == '.' && !parts.isEmpty() && parts.last().end == i);
            ++i;
        }
    }

    int pos = ed->textCursor().positionInBlock();
    int cur = -1;
    for (int i = 0; i < parts.count(); ++i)
    {
        // cursor sticked to the right border belongs to the part as well
        if (pos >= parts[i].start && pos <= parts[i].end)
        {
            cur = i;
            break;
        }
    }
    if (cur == -1)
        return;

    // postgres folds unquoted identifiers to lower case
    auto fold = [](const Part &p) { return p.quoted ? p.text : p.text.toLower(); };
    // being pressed on the 3rd part of a name we script its 2-part prefix
    // (parts[0].dotted is always false, so the qualifier is always in place)
    if (parts[cur].dotted && parts[cur - 1].dotted)
        --cur;
    QString name = fold(parts[cur]);
    QString qualifier = (parts[cur].dotted ? fold(parts[cur - 1]) : QString());

    // The editor's own connection is preferred: same session, hence the same
    // search_path and visibility of the objects created within its uncommitted
    // transaction. It is unusable while busy, and running the service queries
    // inside the user's transaction would be an uninvited side effect, so in
    // those cases a private clone (a separate session) is used instead.
    DbConnection *cn = _connection.get();
    std::unique_ptr<DbConnection> tmpConnection;
    if (_connection->queryState() != QueryState::Inactive ||
        isTimerActive() ||
        !_connection->transactionStatus().isEmpty())
    {
        tmpConnection.reset(_connection->clone());
        if (!tmpConnection->open())
        {
            status(tr("unable to open a service connection to script %1").
                   arg(qualifier.isEmpty() ? name : qualifier + '.' + name));
            return;
        }
        cn = tmpConnection.get();
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString script;
    try
    {
        // The connection may be shared with other widgets (the object tree lends
        // its own connection to the preview pane via highlight()), so its slots
        // must stay in place. Blocking the signals keeps the service queries out
        // of everybody's messages pane without unwiring anything, and restores
        // itself on every exit path, exceptions included. Scripting::execute()
        // reports failures by throwing, so no error is lost here.
        QSignalBlocker blocker(cn);

        auto candidates = Scripting::execute(
                    cn, Scripting::Context::Root, "f4",
                    [&qualifier, &name](const QString &macro) -> QVariant
        {
            // the values are inlined into the query, so quotes must be escaped
            if (macro == "f4.qualifier")
                return qualifier.isEmpty() ?
                            QVariant() : QString(qualifier).replace("'", "''");
            if (macro == "f4.name")
                return QString(name).replace("'", "''");
            return QVariant();
        });

        DataTable *objects = (candidates && !candidates->resultsets.isEmpty() ?
                                  candidates->resultsets.last() : nullptr);
        for (int r = 0; objects && r < objects->rowCount(); ++r)
        {
            QString type = objects->value(r, "type").toString();
            auto c = Scripting::execute(
                        cn, Scripting::Context::Content, type,
                        [objects, r, &type](const QString &macro) -> QVariant
            {
                // Tells the script who asked. A content script may then serve F4
                // differently from a click in the objects tree: table.sql prints
                // the table's indexes instead of the select/insert/update
                // templates, since F4 is about the query being written. Quoted,
                // like $children.names$, so that the script can compare the
                // substituted value with a literal ($gui.context$ = 'F4') and
                // still get a real NULL everywhere else.
                if (macro == "gui.context")
                    return "'F4'";

                // Resultset columns are flat ("schema_name"), while macroses are
                // dotted ("$schema.name$").
                // Content scripts refer to the object itself by its type name, so
                // $table.id$ of table.sql is the object, while $table.id$ of
                // trigger.sql is its hosting relation.
                QString column = (macro.startsWith(type + '.') ?
                                      macro.mid(type.length() + 1) :
                                      QString(macro).replace('.', '_'));
                int ord = objects->getColumnOrd(column);
                if (ord >= 0)
                    return objects->value(r, ord);
                // no particular columns are selected
                if (macro == "children.ids")
                    return "-1";
                return QVariant();
            });

            if (!c)
                continue;
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
            for (const QString &s: qAsConst(c->scripts))
#else
            for (const QString &s: std::as_const(c->scripts))
#endif
            {
                if (s.trimmed().isEmpty())
                    continue;
                if (!script.isEmpty())
                    script += "\n\n";
                script += s.trimmed();
            }
        }
    }
    catch (const QString &err)
    {
        QApplication::restoreOverrideCursor();
        // the failure belongs to the F4 request, not to the user's script, so it
        // must name the object it was about
        onError(tr("unable to script %1: %2").
                arg(qualifier.isEmpty() ? name : qualifier + '.' + name, err));
        return;
    }
    QApplication::restoreOverrideCursor();

    if (script.isEmpty())
    {
        status(tr("%1 not found").arg(qualifier.isEmpty() ? name : qualifier + '.' + name));
        return;
    }

    MainWindow *mainWindow = qobject_cast<MainWindow*>(window());
    if (mainWindow)
        mainWindow->openScriptTab(script, name, _connection->clone());
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
