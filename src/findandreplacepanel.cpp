#include "findandreplacepanel.h"
#include "ui_findandreplacepanel.h"
#include <QPlainTextEdit>
#include <QToolTip>
#include "querywidget.h"

FindAndReplacePanel::FindAndReplacePanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::FindAndReplacePanel), _queryWidget(0)
{
    ui->setupUi(this);
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
    QTextCursor c = find(_queryWidget->textCursor(), 0, sender() == ui->actionFind_previous ? Backward : Forward);
    if (!c.isNull())
        _queryWidget->setTextCursor(c);
}

void FindAndReplacePanel::on_actionReplace_and_find_next_triggered()
{
    on_btnReplace_clicked();
    QTextCursor c = find();
    if (!c.isNull())
        _queryWidget->setTextCursor(c);
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
        QTextDocument::FindFlags searchFlags = 0;
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

QString FindAndReplacePanel::unescape(QString ui_string, bool *err)
{
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
                    res += currentChar;
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
                default:
                    res = tr("Unknown character \\%1\nAvailable escaped characters are \\r, \\n, \\t, \\").arg(nextChar);
                    if (err) *err = true;
                    return res;
                }
            }
            else
                res += currentChar;
        }
        else
            res += currentChar;
    }
    if (err)
        *err = false;
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

    QTextCursor c = find(prev, 0, Check);
    if (c.isNull())
        return;

    if (ui->cbRegexp->isChecked())
    {
        QRegularExpression exp(ui->lineFind->text(), regexpOptions());
        QString tmp = c.selectedText().replace(QChar::ParagraphSeparator, '\n');
        bool err;
        QString repl = unescape(ui->lineReplace->text(), &err);
        if (err)
        {
            QToolTip::showText(ui->lineReplace->mapToGlobal(QPoint(0, -ui->lineReplace->height() * 2)), repl);
            return;
        }
        tmp.replace(exp, repl);
        c.insertText(tmp);
    }
    else c.insertText(ui->lineReplace->text());
    _queryWidget->setTextCursor(c);
}

void FindAndReplacePanel::on_btnReplaceAll_clicked()
{
    QTextCursor c = _queryWidget->textCursor();
    QString src_text = c.hasSelection() ?
                c.selectedText() : _queryWidget->toPlainText();

    QString repl;
    QRegularExpression exp;
    if (ui->cbRegexp->isChecked())
    {
        exp.setPattern(ui->lineFind->text());
        exp.setPatternOptions(regexpOptions());
        bool err;
        repl = unescape(ui->lineReplace->text(), &err);
        if (err)
        {
            QToolTip::showText(ui->lineReplace->mapToGlobal(QPoint(0, -ui->lineReplace->height() * 2)), repl);
            return;
        }
        //^(\w+)\,[^\n]\n
        src_text = src_text.replace(QChar::ParagraphSeparator, '\n');
    }
    else
    {
        QString tofind = ui->lineFind->text().replace(QRegularExpression(R"(([.\^$*+?()[{\\|\]\-]))"), R"(\\1)");
        exp.setPattern(ui->cbWholeWord->isChecked() ? "\\b" + tofind + "\\b" : tofind);
        exp.setPatternOptions(ui->cbCaseSensitive->isChecked() ?
                                  QRegularExpression::NoPatternOption :
                                  QRegularExpression::CaseInsensitiveOption);
        repl = ui->lineReplace->text();
    }

    // just count
    int count = 0;
    QRegularExpressionMatchIterator i = exp.globalMatch(src_text);
    while (i.hasNext())
    {
        i.next();
        ++count;
    }

    // replace
    if (count)
    {
        src_text.replace(exp, repl);

        if (!c.hasSelection())
            c.select(QTextCursor::Document);
        c.insertText(src_text);
        _queryWidget->setTextCursor(c);
    }

    QToolTip::showText(
                ui->btnReplaceAll->mapToGlobal(QPoint(0, -ui->btnReplaceAll->height() * 1.5)),
                tr("%1 occurences replaced").arg(count)
                );
}
