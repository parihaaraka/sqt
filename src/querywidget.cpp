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
#include "settings.h"
#include <QTextBrowser>
#include <QScrollBar>
#include <QCompleter>
#include <QToolTip>
#include <QTimer>
#include "sqlparser.h"
#include "datatable.h"
#include "timechart.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtCore/QTextCodec>
#else
#include <QStringConverter>
#endif

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
    QFile f(fileName);
    if (!f.open(QIODevice::ReadOnly))
    {
        onError(tr("Unable to open %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    _fn = fileName;
    _encoding = encoding;
    QTextStream read_stream(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    read_stream.setCodec(QTextCodec::codecForName(_encoding.toLatin1().data()));
#else
    auto enc = QStringConverter::encodingForName(_encoding);
    if (!enc.has_value())
    {
        onError(tr("Unknown encoding: %1").arg(_encoding));
        return false;
    }
    read_stream.setEncoding(enc.value());
#endif

    if (f.size() > 1024 * 1024 * 5) // do not highlight > 5mb scripts
        dehighlight();
    else
        highlight();
    setPlainText(read_stream.readAll());
    document()->setModified(false);
    f.close();
    return true;
}

bool QueryWidget::saveFile(const QString &fileName, const QString &encoding)
{
    QFile f(fileName);
    if (!f.open(QIODevice::WriteOnly))
    {
        onError(tr("Unable to save %1: %2").arg(fileName, f.errorString()));
        return false;
    }
    _fn = fileName;
    if (!encoding.isEmpty()) _encoding = encoding;
    QTextStream save_stream(&f);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    save_stream.setCodec(QTextCodec::codecForName(_encoding.toLatin1().data()));
#else
    auto enc = QStringConverter::encodingForName(_encoding);
    if (!enc.has_value())
    {
        onError(tr("Unknown encoding: %1").arg(_encoding));
        return false;
    }
    save_stream.setEncoding(enc.value());
#endif

    if (QPlainTextEdit *plain = qobject_cast<QPlainTextEdit*>(_editor))
    {
        // remove trailing space characters without regexp
        // * but save exactly 3 whitespaces! It's about bitbucket's markdown :(
        QTextDocument *doc = plain->document();
        for (QTextBlock it = doc->begin(); it != doc->end(); it = it.next())
        {
            if (it != doc->begin())
                save_stream << '\n';

            QString line = it.text();
            size_t len = line.size();

            int whitespace_count = 0;
            int any_space_count = 0;
            while (len > 0)
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
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
            save_stream << line.midRef(0, len);
#else
            save_stream << line.mid(0, len);
#endif
        }
        //save_stream << plain->toPlainText();
    }
    else if (QTextEdit *rich = qobject_cast<QTextEdit*>(_editor))
        save_stream << rich->toPlainText();
    save_stream.flush();
    f.close();
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

        connect(connection, &DbConnection::queryStateChanged, this, [this](QueryState queryState) {
            // actual query execution time before post-processing
            if (queryState == QueryState::Inactive)
                onMessage(tr("%1: done in %2").arg(QTime::currentTime().toString("HH:mm:ss"), _connection->elapsed()));

            if (MainWindow *mainWindow = qobject_cast<MainWindow*>(window()))
                mainWindow->queryStateChanged(this, queryState);
        }, Qt::QueuedConnection);

        connect(connection, &DbConnection::queryFinished, this, [this]() {
            // print all resultsets structure ready to be used in 'create function returning table(...)'
            QColor resultsetStructureColor = _messages->palette().text().color();
            resultsetStructureColor.setAlphaF(0.6);
            for (const auto res: qAsConst(_connection->_resultsets))
            {
                if (!res->columnCount())
                    continue;

                // initially created models (while fetching data) have not acquired column types yet
                // (unlike _connection->_resultsets)
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
    CodeEditor *editor = initEditor<CodeEditor>(&_editor, this);
    connect(editor, &CodeEditor::completerRequest, this, &QueryWidget::onCompleterRequest, Qt::UniqueConnection);
    editor->setPlainText(text);
}

void QueryWidget::setHtml(const QString &html)
{
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
    connect(_timer, &QTimer::timeout, [this, query] {
        if (_connection->queryState() == QueryState::Inactive)
            _connection->executeAsync(query);
    });
    _connection->executeAsync(query);
    _timer->start(interval);
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

QCompleter* QueryWidget::completer()
{
    static bool initialized = false;
    static QCompleter c;
    if (!initialized)
    {
        initialized = true;
        connect(c.popup()->selectionModel(), &QItemSelectionModel::currentChanged,
                [](const QModelIndex &current, const QModelIndex &) {
            if (current.isValid())
            {
                auto popup = c.popup();
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
                for (const auto &i: qAsConst(arr))
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
    return &c;
}

void QueryWidget::onMessage(const QString &text)
{
    if (!isTimerActive())
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

    if (_querySettings.contains("charts"))
    {
        QVector<TimeChart*> chartWidgets;
        if (!_resSplitter->count())
        {
            // create charts
            QJsonArray jCharts = _querySettings["charts"].toArray();
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
            for (int i = 0; i < _resSplitter->count(); ++i)
                chartWidgets.append(qobject_cast<TimeChart*>(_resSplitter->widget(i)));
        }

        for (auto c: chartWidgets)
        {
            QString xSourceField = c->xSourceField();
            for (auto &n: c->pathNames())
            {
                int ind = table->getColumnOrd(n);
                if (ind != -1)
                {
                    int sfInd = -1;
                    if (!xSourceField.isEmpty())
                    {
                        // if x-source field is specified but not found then skip current chart
                        // (path by path, because I'm too lazy to skip paths too)
                        sfInd = table->getColumnOrd(xSourceField);
                        if (sfInd == -1)
                            continue;
                    }
                    for (int r = 0; r < table->rowCount(); ++r)
                    {
                        bool ok;
                        qreal v = table->getRow(r)[n].toDouble(&ok);
                        if (ok)
                        {
                            if (sfInd == -1)
                                c->appendValue(n, v, QDateTime::currentDateTime());
                            else
                            {
                                QString tsStr = table->getRow(r)[sfInd].toString();
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
        table->clearRows();

    }
    else
    {
        if (_resSplitter->count())
            tv = qobject_cast<QTableView*>(_resSplitter->widget(_resSplitter->count() - 1));

        if (!tv || tv->objectName() != tname)
        {
            tv = new QTableView(_resSplitter);
            tv->verticalHeader()->setDefaultSectionSize(QFontMetrics(tv->font()).lineSpacing());
            tv->horizontalHeader()->viewport()->setMouseTracking(true);
            tv->setObjectName(tname);

            //tv->setContextMenuPolicy(Qt::CustomContextMenu);
            //connect(tv, &QTableView::customContextMenuRequested, this, &QueryWidget::onCustomGridContextMenuRequested);
            //tv->setSelectionMode(QAbstractItemView::ContiguousSelection);
            //tv->addAction(_actionCopy);

            m = new TableModel(_resSplitter);
            m->setObjectName("m" + tname);
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

        if (_connection->queryState() == QueryState::Inactive)
        {
            // disconnect all slots
            disconnect(_connection.get(), nullptr, nullptr, nullptr);
            auto localErrHandler = connect(_connection.get(), &DbConnection::error, this, &QueryWidget::onError);
            c = Scripting::execute(_connection.get(), Scripting::Context::Autocomplete, objectType, env);
            disconnect(localErrHandler);
            setDbConnection(_connection.get());
        }
        else
        {
            // this option does not use current search_path, so you'd better avoid using
            // autocompletion while query is being executed

            std::unique_ptr<DbConnection> tmp_cn(_connection->clone());
            c = Scripting::execute(tmp_cn.get(), Scripting::Context::Autocomplete, objectType, env);
        }
    };

    auto cmpl = completer();
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
        QTimer::singleShot(100, []() {
            completer()->popup()->selectionModel()->setCurrentIndex(
                        completer()->popup()->model()->index(0, 0),
                        QItemSelectionModel::SelectCurrent);
        });
    }
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
