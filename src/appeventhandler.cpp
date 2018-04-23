#include "appeventhandler.h"
#include <QTableView>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QStatusBar>
#include <QPlainTextEdit>
#include "mainwindow.h"
#include "codeeditor.h"

AppEventHandler::AppEventHandler(QObject *parent) : QObject(parent)
{
}

bool AppEventHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((keyEvent->key() == Qt::Key_Comma || keyEvent->key() == Qt::Key_Period) &&
                keyEvent->modifiers().testFlag(Qt::ControlModifier))
        {
            CodeBlockProperties *p = (keyEvent->key() == Qt::Key_Comma ?
                                          Bookmarks::previous() : Bookmarks::next());
            MainWindow *w = qobject_cast<MainWindow*>(QApplication::activeWindow());
            w->activateEditorBlock(p);
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
                foreach(QModelIndex cur, indexes)
                {
                    if (!prev.isValid())
                        ;
                    else if (cur.row() != prev.row())
                        result.append(QString(QChar::LineFeed));
                    else
                        result.append(",");

                    if (cur.data(Qt::TextAlignmentRole).toInt() & Qt::AlignRight)
                        result += cur.data(Qt::EditRole).toString();
                    else
                        result += "'" + cur.data(Qt::EditRole).toString().replace("'","''") + "'";
                    prev = cur;
                }

                QApplication::clipboard()->setText(result);
                return true;
            }
            else if (keyEvent->key() == Qt::Key_F6) // sum numerical values of selected cells
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
                foreach(QModelIndex i, il)
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
            else if (keyEvent->key() == Qt::Key_W && keyEvent->modifiers().testFlag(Qt::ControlModifier))
            {
                QTextOption textOption(edit->document()->defaultTextOption());
                textOption.setWrapMode(textOption.wrapMode() == QTextOption::NoWrap ?
                                           QTextOption::WrapAtWordBoundaryOrAnywhere :
                                           QTextOption::NoWrap);
                edit->document()->setDefaultTextOption(textOption);
                return true;
            }
            else if (keyEvent->matches(QKeySequence::ZoomIn) ||
                     // to enable ctrl+shift+'=' on keyboards without numpad
                     (keyEvent->key() == Qt::Key_Plus && keyEvent->modifiers().testFlag(Qt::ControlModifier)))
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
            QTextOption textOption(edit->document()->defaultTextOption());
            // accurate tab size evaluation
            // TODO move tab size to settings
            textOption.setTabStop(QFontMetrics(edit->font()).width(QString(3 * 100, ' ')) / 100.0);
            if (edit->objectName() != "editCS")
                textOption.setWrapMode(QTextOption::NoWrap);
            edit->document()->setDefaultTextOption(textOption);
            edit->setCursorWidth(2);
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
