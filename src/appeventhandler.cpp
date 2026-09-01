#include "appeventhandler.h"
#include "resourcelocator.h"
#include "decimalsum.h"
#include <QTableView>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QStatusBar>
#include <QPlainTextEdit>
#include <QHeaderView>
#include "mainwindow.h"
#include "codeeditor.h"
#include "misc.h"
#include "settings.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVBoxLayout>
#include "jsonsyntaxhighlighter.h"
#include "querywidget.h"
#include "rowjson.h"
#include "styling.h"
#include "tablemodel.h"

namespace
{

/// The type name of column \a column, as the dbms calls it ("jsonb",
/// "numeric(10,2)") - what tells json and arbitrary-precision columns apart.
/// Only a resultset grid has them; any other view simply gets no hint, which
/// costs nothing but the explicit json/numeric handling.
QString columnTypeName(const QAbstractItemModel *model, int column)
{
    const TableModel *tm = qobject_cast<const TableModel*>(model);
    return (tm ? tm->columnTypeName(column) : QString());
}

} // namespace

AppEventHandler::AppEventHandler(QObject *parent) : QObject(parent)
{
}

bool AppEventHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        const int keyCode = keyEvent->key();
        if ((keyCode == Qt::Key_Comma ||
             keyCode == Qt::Key_Period ||
             keyCode == Qt::Key_L) &&
                keyEvent->modifiers().testFlag(Qt::ControlModifier))
        {
            CodeBlockProperties *p =
                    (keyCode == Qt::Key_Comma ?
                         Bookmarks::previous() :
                         (keyCode == Qt::Key_Period ?
                              Bookmarks::next() :
                              Bookmarks::last()));
            // The filter is application-wide, so the active window is not
            // necessarily the main one: the json viewer is a plain QDialog. A
            // key that means nothing outside the main window is left to its
            // receiver rather than swallowed here.
            MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
            if (!w)
                return QObject::eventFilter(obj, event);
            w->activateEditorBlock(p);
            return true;
        }

        // json viewer (do we need good editor?)
        // * it is used to view any textual value for a while (i need to view long text in cells somehow :)
        // ** json objects being serialized into string values are expanded to become more readable
        if (keyEvent->key() == Qt::Key_J && keyEvent->modifiers().testFlag(Qt::ControlModifier))
        {
            QWidget *window = QApplication::activeWindow();
            // do not open json viewer within itself
            if (window && window->objectName() == "_viewer_")
                return QObject::eventFilter(obj, event);

            QString stringValue;
            // The value has been built as json already (a row, or a selection of
            // cells): it must reach the viewer as it is. The reformatting below
            // would round-trip it through QJsonObject, which keeps its keys
            // sorted alphabetically - and the column order of a row is exactly
            // what makes it readable.
            bool preformatted = false;
            if (QTableView *tv = qobject_cast<QTableView*>(obj))
            {
                // no model - no selection model to ask
                QItemSelectionModel *sm = tv->selectionModel();
                if (sm && tv->model())
                {
                    QModelIndexList indexes = sm->selectedIndexes();
                    // Nothing selected (only a current cell): treat that cell as
                    // the selection, which is what the grid shows as focused.
                    if (indexes.isEmpty() && sm->currentIndex().isValid())
                        indexes.append(sm->currentIndex());

                    const QAbstractItemModel *model = tv->model();
                    stringValue = RowJson::forSelection(
                                model, indexes,
                                [model](int column) { return columnTypeName(model, column); },
                                &preformatted);
                }
            }
            else if (QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(obj))
            {
                stringValue = ed->textCursor().selectedText();
                if (stringValue.isEmpty())
                    stringValue = ed->toPlainText();
            }
            else
                return QObject::eventFilter(obj, event);

            QJsonParseError err;
            err.error = QJsonParseError::NoError;
            QString preparedJsonString = stringValue.trimmed();
            static auto jrepl = QRegularExpression("\\R", QRegularExpression::UseUnicodePropertiesOption);
            // remove line breaks because QJsonDocument::fromJson() doesn't parse formatted json
            preparedJsonString.replace(jrepl, "");
            // A value already built as json (a row, a cell selection) is left
            // alone: parsing it back would sort a row's keys alphabetically and
            // destroy the column order that makes it readable.
            QJsonDocument doc = (preformatted ?
                                     QJsonDocument() :
                                     QJsonDocument::fromJson(preparedJsonString.toUtf8(), &err));

            std::function<QJsonValue(const QJsonValue&)> expandJsonValue;
            expandJsonValue = [&expandJsonValue](const QJsonValue &node) -> QJsonValue {
                // extract json from textual escaped representation
                if (node.isString())
                {
                    QString v = node.toString();
                    if (!v.startsWith('{'))
                        return node;

                    QJsonDocument doc = QJsonDocument::fromJson(v.toUtf8());
                    return doc.isObject() ? doc.object() : node;
                }

                if (node.isArray())
                {
                    QJsonArray new_array;
                    const auto na = node.toArray();
                    for (const auto &i: na)
                    {
                        QJsonValue v(expandJsonValue(i));
                        if (i.isString() && v.isObject())
                            new_array.append(i); // add previous value if it was changed
                        new_array.append(v);
                    }
                    return new_array;
                }

                if (!node.isObject())
                    return node;

                QJsonObject obj = node.toObject();
                for (auto &k: obj.keys())
                {
                    QJsonValueRef jv = obj[k];
                    if (jv.isObject() || jv.isArray())
                        obj.insert(k, expandJsonValue(jv)); // replace
                    else
                    {
                        auto newValue = expandJsonValue(jv);
                        if (newValue.isObject())
                            obj.insert(k + "(nice)", newValue);
                    }
                }
                return obj;
            };

            if (!doc.isNull())
            {
                if (doc.isObject())
                {
                    QJsonValue rootValue(doc.object());
                    doc.setObject(expandJsonValue(rootValue).toObject());
                }
                else if (doc.isArray())
                {
                    QJsonValue rootValue(doc.array());
                    doc.setArray(expandJsonValue(rootValue).toArray());
                }
                stringValue = doc.toJson(QJsonDocument::Indented);
            }


            QDialog *dlg = new QDialog(QApplication::activeWindow());
            dlg->setObjectName("_viewer_");
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            dlg->setWindowTitle(QObject::tr("json"));

            QVBoxLayout *layout = new QVBoxLayout();
            QPlainTextEdit *ed = new QPlainTextEdit(dlg);
            ed->setObjectName("_def_wrap_");
            layout->addWidget(ed);
            if (err.error == QJsonParseError::ParseError::NoError)
                ed->setPlainText(stringValue);
            else
                ed->setPlainText(err.errorString() + '\n' + stringValue);

            QJsonDocument settings;
            try
            {
                // lets use postgresql palette
                settings = readJsonFile(appResources().file("scripts/hl_json.conf"));
            }
            catch (const QString &err)
            {
                // the active window is not necessarily the main one
                if (MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow()))
                    w->onError(err);
            }
            JsonSyntaxHighlighter *hl = new JsonSyntaxHighlighter(settings, ed);
            hl->setDocument(ed->document());

            QStatusBar *status = new QStatusBar(dlg);
            status->setMaximumHeight(QFontMetrics(QApplication::font()).height());
            layout->addWidget(status);
            layout->setContentsMargins(0, 0, 0, 0);
            dlg->setLayout(layout);
            dlg->resize(800, 600);
            dlg->open();
            return true;
        }

        QTableView *tv = qobject_cast<QTableView*>(obj);
        // both branches below work with the selection, which a view
        // without a model does not have
        if (tv && tv->selectionModel())
        {
            if (keyEvent->matches(QKeySequence::Copy))
            {
                QString result;
                QModelIndexList indexes = tv->selectionModel()->selectedIndexes();
                if(indexes.size() < 1)
                    return true;
                std::sort(indexes.begin(), indexes.end());
                QModelIndex prev;
                for (const QModelIndex &cur: std::as_const(indexes))
                {
                    if (!prev.isValid())
                        ;
                    else if (cur.row() != prev.row())
                        result.append(QString(QChar::LineFeed));
                    else
                        result.append(",");

                    QString value = cur.data(Qt::EditRole).toString();
                    if (
                            (cur.data(Qt::TextAlignmentRole).toInt() & Qt::AlignRight) ||
                            // When single cell selected:
                            //   copy plain text if clipboard's value is distinct from current selected value;
                            //   copy quoted literal when clipboard contains it's plain value already.
                            (indexes.size() == 1 && QApplication::clipboard()->text() != value)
                       )
                        result += value;
                    else if (cur.data(Qt::EditRole).isValid())
                        result += "'" + value.replace("'","''") + "'";
                    prev = cur;
                }

                QApplication::clipboard()->setText(result);
                return true;
            }
            else if (keyCode == Qt::Key_F6) // sum numerical values of selected cells
            {
                // Values are summed exactly as they are printed: a decimal
                // accumulator is bound neither by the range of int64 nor by the
                // precision of double, so a wide numeric survives intact and
                // NaN/Infinity are simply not numbers to sum.
                DecimalSum sum;
                QString res;
                const QModelIndexList il = tv->selectionModel()->selectedIndexes();
                for (const QModelIndex &i: il)
                {
                    const QString tmp = i.data(Qt::EditRole).toString();
                    if (!sum.add(tmp))
                        continue;
                    res += (res.isEmpty() ? "" : " + ") + tmp;
                }

                const int count = sum.count();
                const QString totalAmount = sum.toString(QLocale());


                if (count)
                    QApplication::clipboard()->setText(res);
                MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
                if (w)
                {
                    auto msg = QString("%1 (%2 cells)").arg(totalAmount).arg(count);
                    if (w->statusBar())
                        w->statusBar()->showMessage(msg, 1000*15);
                    w->onMessage(msg);
                }
                return true;
            }
        }
        else if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit*>(obj))
        {
            if (keyEvent->matches(QKeySequence::Copy) && edit->textCursor().hasSelection())
            {
                // Application event filters run before the receiver's own
                // event filters, so this used to swallow Ctrl+C before
                // CodeEditor ever saw it - and edit->textCursor() is just
                // the *main* cursor, which is why copying a multi-cursor
                // selection only ever yielded the main cursor's part.
                // Let the editor handle those itself.
                CodeEditor *codeEdit = qobject_cast<CodeEditor*>(edit);
                if (codeEdit && codeEdit->hasMultipleCursors())
                    return QObject::eventFilter(obj, event);

                QApplication::clipboard()->setText(edit->textCursor().selectedText().replace(QChar::ParagraphSeparator, '\n'));
                return true;
            }
            else if (keyCode == Qt::Key_W &&
                     keyEvent->modifiers().testFlag(Qt::ControlModifier) &&
                     keyEvent->modifiers().testFlag(Qt::ShiftModifier))
            {
                QTextOption textOption(edit->document()->defaultTextOption());
                textOption.setWrapMode(textOption.wrapMode() == QTextOption::NoWrap ?
                                           QTextOption::WrapAtWordBoundaryOrAnywhere :
                                           QTextOption::NoWrap);
                edit->document()->setDefaultTextOption(textOption);
                return true;
            }
            else if (keyEvent->matches(QKeySequence::ZoomIn) ||
                     // to use ctrl+shift+'=' on keyboards without numpad
                     (keyCode == Qt::Key_Plus && keyEvent->modifiers().testFlag(Qt::ControlModifier)))
            {
                edit->zoomIn();
                return true;
            }
            else if (keyEvent->matches(QKeySequence::ZoomOut))
            {
                edit->zoomOut();
                return true;
            }
        }
    }
    else if (event->type() == QEvent::FontChange)
    {
        //auto *e = qobject_cast<CodeEditor*>(obj);
        if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit*>(obj))
        {
            // apply tab size
            int indentSize = SqtSettings::value("indentSize", 3).toInt();
            QTextOption textOption(edit->document()->defaultTextOption());
            // accurate tab size evaluation
            textOption.setTabStopDistance(QFontMetrics(edit->font())
                                          .horizontalAdvance(QString(indentSize * 100, ' ')) / 100.0);
            if (edit->objectName() != "editCS" && edit->objectName() != "_def_wrap_")
                textOption.setWrapMode(QTextOption::NoWrap);

            // Both levels checked: an editor reparented (or not yet parented)
            // would otherwise dereference a null first parent().
            QObject *parent = edit->parent();
            auto *p = (parent ? parent->parent() : nullptr);
            auto *qw = p ? qobject_cast<QueryWidget*>(p) : nullptr;
            bool flagsChanged = false;
            if (qw && qw->is_sql_hl(edit))
            {
                QTextOption::Flags currentFlags = textOption.flags();
                QTextOption::Flags prevFlags = currentFlags;
                if (SqtSettings::value("visualizeWhitespace", false).toBool())
                    currentFlags |= QTextOption::ShowTabsAndSpaces;
                else
                    currentFlags &= ~QTextOption::ShowTabsAndSpaces;
                textOption.setFlags(currentFlags);
                flagsChanged = prevFlags != currentFlags;
            }
            edit->document()->setDefaultTextOption(textOption);
            if (qw && flagsChanged)
                qw->rehighlight();
        }
        else if (QTableView *tv = qobject_cast<QTableView*>(obj))
        {
            // adjust grid line height
            tv->verticalHeader()->setDefaultSectionSize(comfortableRowHeight(tv));
        }
    }
    else if (event->type() == QEvent::Paint)
    {
        // Runs ahead of QTableView's own paint handling (this filter is
        // installed on qApp, so it sees every event before its target does),
        // which is what paintPixelPerfectGrid() needs: Qt draws the grid
        // before the cells, and a cell's own opaque background is what then
        // covers this line's leftover pixel inside that cell, exactly the
        // way it would with the native grid this replaces (see
        // setShowGrid(false) wherever a results QTableView is set up).
        if (QWidget *viewport = qobject_cast<QWidget*>(obj))
        {
            if (QTableView *tv = qobject_cast<QTableView*>(viewport->parentWidget());
                    tv && tv->viewport() == viewport)
                paintPixelPerfectGrid(tv);
        }
    }
    else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
    {
        MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
        if (w)
            return w->eventFilter(obj, event); // handle actions state if needed
    }

    return QObject::eventFilter(obj, event);
}
