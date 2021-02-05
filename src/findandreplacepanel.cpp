#include "findandreplacepanel.h"
#include "ui_findandreplacepanel.h"
#include <QPlainTextEdit>
#include <QToolTip>
#include "querywidget.h"
#include "codeeditor.h"
#include "set"

FindAndReplacePanel::FindAndReplacePanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FindAndReplacePanel), _queryWidget(nullptr)
{
    ui->setupUi(this);
    setContentsMargins(1, 1, 1, 1);
    setLayout(ui->gridLayout);
    QAction *actionEsc = new QAction(this);
    actionEsc->setShortcut(QKeySequence(Qt::Key_Escape));
    connect(actionEsc, &QAction::triggered, this, &QWidget::hide);
    addAction(actionEsc);
    ui->actionFind_next->setShortcuts(QKeySequence::FindNext);
    ui->actionFind_previous->setShortcuts(QKeySequence::FindPrevious);
    ui->btnNext->setDefaultAction(ui->actionFind_next);
    ui->btnPrevious->setDefaultAction(ui->actionFind_previous);
    ui->btnReplaceFindNext->setDefaultAction(ui->actionReplace_and_find_next);
    connect(ui->actionFind_next, &QAction::triggered, this, &FindAndReplacePanel::findTriggered);
    connect(ui->actionFind_previous, &QAction::triggered, this, &FindAndReplacePanel::findTriggered);
    connect(ui->lineFind, SIGNAL(returnPressed()), ui->actionFind_next, SIGNAL(triggered()));
    connect(ui->lineReplace, SIGNAL(returnPressed()), ui->actionReplace_and_find_next, SIGNAL(triggered()));
    setFocusProxy(ui->lineFind);
    ui->lineFind->installEventFilter(this);
    ui->lineReplace->installEventFilter(this);
}

FindAndReplacePanel::~FindAndReplacePanel()
{
    delete ui;
}

QList<QAction *> FindAndReplacePanel::actions()
{
    return QList<QAction*>() << ui->actionFind_next << ui->actionFind_previous << ui->actionReplace_and_find_next;
}

void FindAndReplacePanel::setEditor(QueryWidget *qw)
{
    _queryWidget = qw;
    refreshActions();
}

void FindAndReplacePanel::refreshActions()
{
    bool is_ro = (_queryWidget ? _queryWidget->isReadOnly() : true);
    ui->lineReplace->setEnabled(!is_ro);
    ui->btnReplace->setEnabled(!is_ro);
    ui->btnReplaceAll->setEnabled(!is_ro);
    ui->btnReplaceFindNext->setEnabled(!is_ro);
    ui->actionReplace_and_find_next->setEnabled(!is_ro);
    ui->actionFind_next->setEnabled(_queryWidget);
    ui->actionFind_previous->setEnabled(_queryWidget);
}

void FindAndReplacePanel::findTriggered()
{
    QTextCursor c = find(_queryWidget->textCursor(), nullptr, sender() == ui->actionFind_previous ? Backward : Forward);
    if (!c.isNull())
    {
        _queryWidget->setTextCursor(c);
        _queryWidget->setFocus();
    }
}

void FindAndReplacePanel::on_actionReplace_and_find_next_triggered()
{
    on_btnReplace_clicked();
    QTextCursor c = find();
    if (!c.isNull())
    {
        _queryWidget->setTextCursor(c);
        _queryWidget->setFocus();
    }
}

QTextCursor FindAndReplacePanel::find(const QTextCursor &cursor, bool *nextPass, FindMode mode)
{
    bool res = false;
    if (nextPass) *nextPass = false;
    if (!_queryWidget || ui->lineFind->text().isEmpty())
        return QTextCursor();

    QTextCursor cur_cursor = (cursor.isNull() ? _queryWidget->textCursor() : cursor);
    QTextCursor new_cursor = _queryWidget->textCursor();
    if (ui->cbRegexp->checkState() == Qt::Checked)
    {
        QRegularExpression exp(ui->lineFind->text(), regexpOptions());
        if (!exp.isValid())
            QToolTip::showText(ui->lineFind->mapToGlobal(QPoint(0, -ui->lineFind->height() * 1.5)), exp.errorString());
        else
        {
            QString src = _queryWidget->toPlainText();
            if (mode == Backward)
            {
                int offset = 0;
                // find last match before current cursor
                do
                {
                    QRegularExpressionMatch m = exp.match(src, offset);
                    if (m.hasMatch())
                    {
                        if (cur_cursor.anchor() < m.capturedEnd()) // cursor skipped
                        {
                            // nothing found -> move cursor to the end
                            if (!res && cur_cursor.anchor() != src.length())
                            {
                                cur_cursor.setPosition(src.length());
                                if (nextPass)
                                    *nextPass = true;
                            }
                            else
                                break;
                        }
                        res = true;
                        new_cursor.setPosition(m.capturedStart());
                        new_cursor.setPosition(m.capturedEnd(), QTextCursor::KeepAnchor);
                        offset = m.capturedEnd();
                    }
                    else
                        break;
                }
                while (true);
            }
            else // Forward, Check
            {
                QRegularExpressionMatch m =
                        (mode == Forward ? exp.match(src, cur_cursor.position()) :
                                           exp.match(cur_cursor.selectedText().replace(QChar::ParagraphSeparator, '\n')));
                if (!m.hasMatch() && mode == Forward)
                {
                    m = exp.match(src);
                    if (nextPass)
                        *nextPass = true;
                }
                if (m.hasMatch())
                {
                    res = true;
                    if (mode == Forward)
                    {
                        new_cursor.setPosition(m.capturedStart());
                        new_cursor.setPosition(m.capturedEnd(), QTextCursor::KeepAnchor);
                    }
                    else
                    {
                        new_cursor.setPosition(cur_cursor.selectionStart() + m.capturedStart());
                        new_cursor.setPosition(new_cursor.selectionStart() + m.capturedLength(), QTextCursor::KeepAnchor);
                    }
                }
            }
        }
    }
    else
    {
        QTextDocument::FindFlags searchFlags;
        if (mode == Backward)
            searchFlags |= QTextDocument::FindBackward;
        if (ui->cbCaseSensitive->checkState() == Qt::Checked)
            searchFlags |= QTextDocument::FindCaseSensitively;
        if (ui->cbWholeWord->checkState() == Qt::Checked)
            searchFlags |= QTextDocument::FindWholeWords;

        if (mode == Check)
        {
            new_cursor = _queryWidget->document()->find(ui->lineFind->text(), cur_cursor.anchor(), searchFlags);
            if (!new_cursor.isNull() && new_cursor.selectedText() == cur_cursor.selectedText())
            {
                new_cursor = cur_cursor;
                res = true;
            }
        }
        else
        {
            new_cursor = _queryWidget->document()->find(ui->lineFind->text(), cur_cursor, searchFlags);
            res = !new_cursor.isNull();
            if (!res)
            {
                new_cursor = _queryWidget->textCursor();
                new_cursor.movePosition(mode == Forward ? QTextCursor::Start : QTextCursor::End);
                new_cursor = _queryWidget->document()->find(ui->lineFind->text(), new_cursor, searchFlags);
                res = !new_cursor.isNull();
                if (nextPass)
                    *nextPass = true;
            }
        }
    }
    return (res ? new_cursor : QTextCursor());
}

QRegularExpression::PatternOptions FindAndReplacePanel::regexpOptions()
{
    QRegularExpression::PatternOptions opt = QRegularExpression::NoPatternOption;
    if (ui->cbCaseSensitive->checkState() != Qt::Checked)
        opt |= QRegularExpression::CaseInsensitiveOption;
    if (ui->cbRegexpM->checkState() == Qt::Checked)
        opt |= QRegularExpression::MultilineOption;
    if (ui->cbRegexpS->checkState() == Qt::Checked)
        opt |= QRegularExpression::DotMatchesEverythingOption;
    if (ui->cbRegexpU->checkState() == Qt::Checked)
        opt |= QRegularExpression::UseUnicodePropertiesOption;
    return opt;
}

QVector<ReplaceChunk> FindAndReplacePanel::unescape(QString ui_string, QString *err)
{
    if (err)
        err->clear();

    QVector<ReplaceChunk> array;
    QString res;
    for (int i = 0; i < ui_string.length(); ++i)
    {
        QChar currentChar = ui_string.at(i);
        if (currentChar == '\\')
        {
            if (ui_string.length() > i + 1)
            {
                QChar nextChar = ui_string.at(i + 1);
                if (nextChar.isDigit())
                {
                    if (!res.isEmpty())
                        array.append(ReplaceChunk{-1, res});
                    array.append(ReplaceChunk{nextChar.digitValue(), ""});
                    ++i;
                    res.clear();
                    continue;
                }

                switch (nextChar.toLatin1())
                {
                case '\\':
                    ++i;
                    res += currentChar;
                    break;
                case 'r':
                    ++i;
                    res += '\r';
                    break;
                case 'n':
                    ++i;
                    res += '\n';
                    break;
                case 't':
                    ++i;
                    res += '\t';
                    break;
                case 'g':  //   \g{10} - backreference
                {
                    nextChar = ui_string.at(i + 2);
                    int refNum = -1;
                    if (nextChar == '{')
                    {
                        auto tmp = ui_string.mid(i + 3).toStdString();
                        const char *num_ptr = tmp.c_str();
                        char *end;
                        refNum = static_cast<int>(strtol(num_ptr, &end, 10));
                        if (end > num_ptr && *end == '}')
                        {
                            if (!res.isEmpty())
                                array.append(ReplaceChunk{-1, res});
                            array.append(ReplaceChunk{refNum, ""});
                            i += (end - num_ptr) + 3;
                            res.clear();
                        }
                        else
                            refNum = -1;
                    }
                    if (refNum == -1)
                    {
                        if (err)
                            *err = tr("Incorrect backreference. Must be \\g{<group number>}.");
                        return array;
                    }
                    break;
                }
                default:
                    if (err)
                        *err = tr("Unknown character \\%1\nAvailable escaped characters are \\r, \\n, \\t, \\.\n"
                                  "Backreferences: \\<1-digit group number> or \\g{<group number>}").arg(nextChar);
                    return array;
                }
            }
            else
                res += currentChar;
        }
        else
            res += currentChar;
    }
    if (!res.isEmpty())
        array.append(ReplaceChunk{-1, res});
    return array;
}

QString FindAndReplacePanel::replaceMatch(const QRegularExpressionMatch &match, const QVector<ReplaceChunk> &repl)
{
    QString res;
    for (const auto &r: repl)
    {
        res.append(r.refNum == -1 ? r.value : match.captured(r.refNum));
    }
    return res;
}

bool FindAndReplacePanel::eventFilter(QObject *target, QEvent *event)
{
    QLineEdit *lineEdit = qobject_cast<QLineEdit*>(target);
    if (event->type() == QEvent::FocusIn && lineEdit)
    {
        lineEdit->selectAll();
    }
    return QObject::eventFilter(target, event);
}

void FindAndReplacePanel::on_cbRegexp_toggled(bool checked)
{
    ui->cbWholeWord->setEnabled(!checked);
    ui->cbRegexpM->setEnabled(checked);
    ui->cbRegexpS->setEnabled(checked);
    ui->cbRegexpU->setEnabled(checked);
}

void FindAndReplacePanel::on_btnReplace_clicked()
{
    QTextCursor prev = _queryWidget->textCursor();
    if (!prev.hasSelection())
        return;

    QTextCursor c = find(prev, nullptr, Check);
    if (c.isNull())
        return;

    if (ui->cbRegexp->isChecked())
    {
        QRegularExpression exp(ui->lineFind->text(), regexpOptions());
        QString tmp = c.selectedText().replace(QChar::ParagraphSeparator, '\n');
        QString err;
        auto repl = unescape(ui->lineReplace->text(), &err);
        if (!err.isEmpty())
        {
            QToolTip::showText(ui->lineReplace->mapToGlobal(QPoint(0, -ui->lineReplace->height() * 2)), err);
            return;
        }

        QRegularExpressionMatch m = exp.match(tmp);
        if (m.hasMatch())
        {
            tmp = replaceMatch(m, repl);
            c.insertText(tmp);
        }
    }
    else
        c.insertText(ui->lineReplace->text());
    _queryWidget->setTextCursor(c);
}

void FindAndReplacePanel::on_btnReplaceAll_clicked()
{
    // paragraph by paragraph replacement is very slow,
    // so lets do replacement on solid textual buffer

    QTextCursor c = _queryWidget->textCursor();
    QString src_text = c.hasSelection() ?
                c.selectedText() :
                _queryWidget->toPlainText();

    struct BookmarkInfo
    {
        int blockNumber;
        CodeBlockProperties *prop;
    };

    // bookmarks by block position
    int selectionPos = c.hasSelection() ? c.selectionStart() : 0;
    QMap<int, BookmarkInfo> bookmarks;
    {
        QTextBlock block = c.hasSelection() ?
                    _queryWidget->document()->findBlock(c.selectionStart()) :
                    _queryWidget->document()->begin();
        while (block.isValid())
        {
            // do not process bookmarks out of selection
            if (c.hasSelection() && c.selectionEnd() <= block.position())
                break;

            if (block.userData())
                bookmarks.insert(block.position(),
                                 BookmarkInfo{
                                     block.blockNumber(),
                                     static_cast<CodeBlockProperties*>(block.userData())});
            block = block.next();
        }
    }

    // если при замене увеличилось количество блоков, то сдвигать
    // закладки *ниже* исходного выделения (на следующих за ним блоках)
    // на дельту блоков

    // если при замене уменьшилось количество блоков, то:
    // 1) очищать закладки на убавившихся блоках;
    // 2) сдвигать закладки *ниже* исходного выделения
    //    на дельту блоков

    QVector<ReplaceChunk> replParts;
    QRegularExpression exp;
    if (ui->cbRegexp->isChecked())
    {
        exp.setPattern(ui->lineFind->text());
        exp.setPatternOptions(regexpOptions());
        QString err;
        replParts = unescape(ui->lineReplace->text(), &err);
        if (!err.isEmpty())
        {
            QToolTip::showText(ui->lineReplace->mapToGlobal(QPoint(0, -ui->lineReplace->height() * 2)), err);
            return;
        }
        // make \n work
        src_text.replace(QChar::ParagraphSeparator, '\n');
    }
    else
    {
        QString tofind = ui->lineFind->text().replace(QRegularExpression(R"(([.\^$*+?()[{\\|\]\-]))"), R"(\\1)");
        exp.setPattern(ui->cbWholeWord->isChecked() ? "\\b" + tofind + "\\b" : tofind);
        exp.setPatternOptions(ui->cbCaseSensitive->isChecked() ?
                                  QRegularExpression::NoPatternOption :
                                  QRegularExpression::CaseInsensitiveOption);
        replParts.append(ReplaceChunk{-1, ui->lineReplace->text()});
    }

    int count = 0;
    int offset = c.hasSelection() ? c.selectionStart() : 0;
    QRegularExpressionMatchIterator i = exp.globalMatch(src_text);
    QSet<int> bookmarksToRemove;
    if (i.hasNext())
    {
        QString dst;
        int prevPos = 0;
        dst.reserve(src_text.length());
        while (i.hasNext())
        {
            ++count;
            QRegularExpressionMatch match = i.next();
            QString tmp = replaceMatch(match, replParts);
            dst.append(src_text.midRef(prevPos, match.capturedStart() - prevPos));
            dst.append(tmp);
            prevPos = match.capturedEnd();

            int prevLF = match.captured().count('\n');
            int newLF = tmp.count('\n');
            int deltaLF = newLF - prevLF;

            // search for the first affected bookmark
            auto bm_it = bookmarks.lowerBound(offset + match.capturedStart());
            if (bm_it == bookmarks.end())
                continue;

            int matchBlockNumber = _queryWidget->document()->findBlock(selectionPos + match.capturedStart()).blockNumber();
            int capturedEnd = selectionPos + match.capturedStart() + match.capturedLength();
            while (bm_it != bookmarks.end())
            {
                int bm_pos = bm_it.key();
                // bookmark within truncated part of replaced text
                if (bm_it->blockNumber > matchBlockNumber + newLF && bm_it->blockNumber <= matchBlockNumber + prevLF)
                    bookmarksToRemove.insert(bm_it.key());
                else if (bm_pos > capturedEnd)
                    bm_it->blockNumber += deltaLF;
                ++bm_it;
            }
        }
        dst.append(src_text.midRef(prevPos));
        Bookmarks::suspend();

        // replace selection or entire text
        if (!c.hasSelection())
            c.select(QTextCursor::Document);
        c.insertText(dst);

        // All bookmarks from replaced area are destroyed,
        // so we need to revitalize the pointers within _bookmarks list.
        // This risky magic is used to preserve bokmarks order.
        for (auto it = bookmarks.begin(); it != bookmarks.end(); ++it)
        {
            // the pointer wil be dropped from _bookmarks on resume
            if (bookmarksToRemove.contains(it.key()))
                continue;

            // create new bookmark on the block which was bookmarked before replace
            CodeBlockProperties *prop = new CodeBlockProperties(
                        dynamic_cast<CodeEditor*>(_queryWidget->editor()),
                        it->prop);
            auto block = _queryWidget->document()->findBlockByNumber(it->blockNumber);
            block.setUserData(prop);
        }
        Bookmarks::resume();
    }

    QToolTip::showText(
                ui->btnReplaceAll->mapToGlobal(
                    QPoint(0, static_cast<int>(-ui->btnReplaceAll->height() * 1.5))),
                tr("%1 occurences replaced").arg(count)
                );
}
