#include "appeventhandler.h"
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
#include "settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QVBoxLayout>
#include "jsonsyntaxhighlighter.h"

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
            MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
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
            if (QTableView *tv = qobject_cast<QTableView*>(obj))
                stringValue = tv->selectionModel()->currentIndex().data().toString();
            else if (QPlainTextEdit *ed = qobject_cast<QPlainTextEdit*>(obj))
            {
                stringValue = ed->textCursor().selectedText();
                if (stringValue.isEmpty())
                    stringValue = ed->toPlainText();
            }
            else
                return QObject::eventFilter(obj, event);

            QJsonParseError err;
            QString preparedJsonString = stringValue.trimmed();
            // remove line breaks because QJsonDocument::fromJson() doesn't parse formatted json
            preparedJsonString.replace(QRegularExpression("\\R", QRegularExpression::UseUnicodePropertiesOption), "");
            QJsonDocument doc = QJsonDocument::fromJson(preparedJsonString.toUtf8(), &err);

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
                    for (auto i: node.toArray())
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
            ed->setPlainText(err.errorString() + '\n' + stringValue);

            JsonSyntaxHighlighter *hl = new JsonSyntaxHighlighter(ed);
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

        if (QTableView *tv = qobject_cast<QTableView*>(obj))
        {
            if (keyEvent->matches(QKeySequence::Copy))
            {
                QString result;
                QModelIndexList indexes = tv->selectionModel()->selectedIndexes();
                if(indexes.size() < 1)
                    return true;
                std::sort(indexes.begin(), indexes.end());
                QModelIndex prev;
                for (const QModelIndex &cur: indexes)
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
                    else
                        result += "'" + value.replace("'","''") + "'";
                    prev = cur;
                }

                QApplication::clipboard()->setText(result);
                return true;
            }
            else if (keyCode == Qt::Key_F6) // sum numerical values of selected cells
            {
                // lets try to sum big numerics precisely
                double sumDouble = 0;
                qlonglong sumInt = 0;
                qlonglong sumFrac = 0;
                int maxScale = 0;
                bool ok;
                int count = 0;
                QString res;
                QModelIndexList il = tv->selectionModel()->selectedIndexes();
                for (const QModelIndex &i: il)
                {
                    double val = i.data(Qt::EditRole).toDouble(&ok);
                    if (ok)
                    {
                        QString tmp = i.data(Qt::EditRole).toString();
                        int dotPos = tmp.indexOf('.');
                        if (dotPos == -1 && val != static_cast<qlonglong>(val)) // floating point exponential form
                        {
                            sumDouble += val;
                            maxScale = std::max(maxScale, 6);
                        }
                        else if (dotPos == -1) // integer
                            sumInt += val;
                        else    // decimal
                        {
                            sumInt += tmp.mid(0, dotPos).toLongLong();
                            maxScale = std::max(maxScale, tmp.length() - dotPos - 1);
                            qlonglong frac = (tmp + "000000000000000000").mid(dotPos + 1, 18).toLongLong();
                            if (val < 0)
                            {
                                sumFrac -= frac;
                                if (sumFrac < 0)
                                {
                                    --sumInt;
                                    sumFrac = 1000000000000000000LL + sumFrac;
                                }
                            }
                            else
                            {
                                sumFrac += frac;
                                if (sumFrac >= 1000000000000000000LL)
                                {
                                    ++sumInt;
                                    sumFrac = sumFrac - 1000000000000000000LL;
                                }
                            }
                        }

                        ++count;
                        res += (res.isEmpty() ? "" : " + ") + tmp;
                    }
                }

                QString totalAmount;
                QLocale locale;
                if (sumDouble != 0) //if there was a floating point, cast result to double
                {
                    double sum = sumDouble + sumInt + sumFrac/1000000000000000000.0;
                    totalAmount = locale.toString(sum, 'f', maxScale);
                }
                else
                {
                    totalAmount = locale.toString(sumInt);
                    if (maxScale)
                        totalAmount += locale.decimalPoint() + QString::number(sumFrac).mid(0, maxScale);
                }

                if (count)
                    QApplication::clipboard()->setText(res);
                QMainWindow *w = qobject_cast<QMainWindow*>(QApplication::activeWindow());
                if (w && w->statusBar())
                    w->statusBar()->showMessage(QString("%1 (%2 cells)").arg(totalAmount).arg(count), 1000*15);
                return true;
            }
        }
        else if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit*>(obj))
        {
            if (keyEvent->matches(QKeySequence::Copy) && edit->textCursor().hasSelection())
            {
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
        if (QPlainTextEdit *edit = qobject_cast<QPlainTextEdit*>(obj))
        {
            // apply tab size
            int tabSize = SqtSettings::value("tabSize", 3).toInt();
            QTextOption textOption(edit->document()->defaultTextOption());
            // accurate tab size evaluation
            textOption.setTabStop(QFontMetrics(edit->font()).width(QString(tabSize * 100, ' ')) / 100.0);
            if (edit->objectName() != "editCS" && edit->objectName() != "_def_wrap_")
                textOption.setWrapMode(QTextOption::NoWrap);
            edit->document()->setDefaultTextOption(textOption);
            edit->setCursorWidth(2);
        }
        else if (QTableView *tv = qobject_cast<QTableView*>(obj))
        {
            // adjust grid line height
            int minHeight = tv->verticalHeader()->minimumSectionSize();
            int comfortableHeight = qRound(tv->fontMetrics().height() * 1.35);
            tv->verticalHeader()->setDefaultSectionSize(qMax(minHeight, comfortableHeight));
        }
    }
    else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut)
    {
        MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
        if (w)
            return w->eventFilter(obj, event); // handle actions state if needed
    }

    return false;
}
