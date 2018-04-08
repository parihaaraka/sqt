#include "appeventhandler.h"
#include <QTableView>
#include <QKeyEvent>
#include <QApplication>
#include <QClipboard>
#include <QMainWindow>
#include <QStatusBar>
#include <QPlainTextEdit>

AppEventHandler::AppEventHandler(QObject *parent) : QObject(parent)
{
}

bool AppEventHandler::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
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
                bool ok;
                double sum = 0;
                int count = 0;
                QString res;
                QModelIndexList il = tv->selectionModel()->selectedIndexes();
                foreach(QModelIndex i, il)
                {
                    double val = i.data(Qt::EditRole).toDouble(&ok);
                    if (ok)
                    {
                        sum += val;
                        count++;
                        res += (res.isEmpty() ? "" : " + ") + i.data(Qt::EditRole).toString();
                    }
                }
                if (count)
                    QApplication::clipboard()->setText(res);
                QMainWindow *w = qobject_cast<QMainWindow*>(QApplication::activeWindow());
                if (w && w->statusBar())
                    w->statusBar()->showMessage(QString("%1 (%2 cells)").arg(QLocale().toString(sum, 'f', 2)).arg(count), 1000*15);
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
            // TODO move tab stop size to settings
            textOption.setTabStop(QFontMetrics(edit->font()).width(QString(3 * 100, ' ')) / 100.0);
            if (edit->objectName() != "editCS")
                textOption.setWrapMode(QTextOption::NoWrap);
            edit->document()->setDefaultTextOption(textOption);
            edit->setCursorWidth(2);
        }
    }

    return QObject::eventFilter(obj, event);
}
