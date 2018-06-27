#include <QtGlobal>
#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QDesktopServices>
#include <QCompleter>
#include <QAbstractItemView>
#include "settings.h"

#define RIGHT_MARGIN 2
#define ICON_PLACE_WIDTH 13

static QList<CodeBlockProperties*> _bookmarks;
static float _lastUsedBookmarkPos = 0;

class LeftSideBar : public QWidget
{
public:
    LeftSideBar(CodeEditor *editor) : QWidget(editor), _codeEditor(editor) {}
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    CodeEditor *_codeEditor;
};

QSize LeftSideBar::sizeHint() const {
    return QSize(_codeEditor->leftSideBarWidth(), 0);
}

void LeftSideBar::paintEvent(QPaintEvent *event) {
    _codeEditor->leftSideBarPaintEvent(event);
}

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    _leftSideBar = new LeftSideBar(this);
    _hlTimer = new QTimer(this);
    _hlTimer->setInterval(20);
    _hlTimer->setSingleShot(true);
    connect(_hlTimer, &QTimer::timeout, this, &CodeEditor::onHlTimerTimeout);
    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLeftSideBarWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLeftSideBar);
    connect(this, &CodeEditor::cursorPositionChanged, [this]() {
        _leftSideBar->update();
        auto selections = extraSelections();
        // remove old line indicator
        if (!selections.isEmpty() && selections.back().format.property(QTextFormat::FullWidthSelection).toBool())
            selections.pop_back();
        // add new line indicator
        selections += currentLineSelection();
        setExtraSelections(selections);
        _hlTimer->start();
    });
    connect(this, &CodeEditor::textChanged, [this]() {
        setExtraSelections(currentLineSelection());
        _hlTimer->start();
    });
    connect(this, &CodeEditor::selectionChanged, _hlTimer, static_cast<void(QTimer::*)(void)>(&QTimer::start));

    updateLeftSideBarWidth();
    installEventFilter(this);
}

void CodeEditor::leftSideBarPaintEvent(QPaintEvent *event)
{
    int curBlock = textCursor().block().blockNumber();
    QPainter painter(_leftSideBar);

    // fill background (try to make it ready to dark color scheme)
    QColor windowColor = palette().base().color();
    if (windowColor.toHsv().valueF() > 0.5) // light color
        windowColor = windowColor.darker(105);
    else
        windowColor = windowColor.lighter(105);
    painter.fillRect(event->rect(), windowColor);

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = int(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = int(top + blockBoundingRect(block).height());

    QFont curFont(font());
    int bCount = blockCount();
    // draw line numbers
    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            if (curBlock == blockNumber)
            {
                QFont curLineFont = curFont;
                curLineFont.setBold(true);
                painter.setFont(curLineFont);
            }
            else
                painter.setFont(curFont);

            QColor penColor = palette().windowText().color();
            QString label;
            if (blockNumber || bCount < 10)
            {
                penColor.setAlphaF(curBlock == blockNumber ? 0.7 : 0.4);
                label = QString::number(blockNumber + 1);
            }
            else
            {
                penColor.setGreenF(0.5);
                label = "↓" + QString::number(bCount);
            }

            painter.setPen(penColor);
            painter.drawText(0,
                             top,
                             _leftSideBar->width() - RIGHT_MARGIN,
                             fontMetrics().height(),
                             Qt::AlignRight | Qt::AlignVCenter,
                             label);
            CodeBlockProperties *prop = static_cast<CodeBlockProperties*>(block.userData());
            if (prop)
            {
                QPixmap imgBookmark(":/img/bookmark.png");
                painter.drawPixmap((ICON_PLACE_WIDTH - imgBookmark.width())/2,
                                   top,
                                   imgBookmark.width(),
                                   imgBookmark.height(),
                                   imgBookmark);
            }
        }

        block = block.next();
        top = bottom;
        bottom = top + int(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

int CodeEditor::leftSideBarWidth() const
{
    return ICON_PLACE_WIDTH + RIGHT_MARGIN +
            fontMetrics().width(QString::number(blockCount()));
}

QString CodeEditor::text() const
{
#if QT_VERSION >= 0x050900
    return document()->toRawText();
#else
    return document()->toPlainText();
#endif
}

void CodeEditor::setCompleter(QCompleter *completer)
{
    if (_completer)
        disconnect(_completer, nullptr, this, nullptr);

    _completer = completer;

    if (!_completer)
        return;

    _completer->setWidget(this);
    QObject::connect(_completer, static_cast<void(QCompleter::*)(const QString &)>(&QCompleter::activated),
                     this, &CodeEditor::insertCompletion);
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    _leftSideBar->setGeometry(QRect(cr.left(), cr.top(), leftSideBarWidth(), cr.height()));
}

bool CodeEditor::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type())
    {
    case QEvent::FontChange:
        _leftSideBar->setFont(font());
        updateLeftSideBarWidth();
        break;
    case QEvent::KeyPress:
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_F1)
        {
            QString url = SqtSettings::value(keyEvent->modifiers().testFlag(Qt::ShiftModifier) ?
                                                 "shiftF1url" : "f1url").toString();
            QDesktopServices::openUrl(QUrl(url));
            return true;
        }

        if (isReadOnly())
            break;

        if (keyEvent->key() == Qt::Key_Insert && keyEvent->modifiers() == Qt::NoModifier)
        {
            setOverwriteMode(!overwriteMode());
            return true;
        }

        QTextCursor c = textCursor();

        if (keyEvent->key() == Qt::Key_Return)
        {
            // previous indentation
            c.removeSelectedText();
            QRegularExpression indent_regex("(^\\s*)(?=[^\\s\\r\\n]+)");
            QTextCursor prev_c = document()->find(indent_regex, c, QTextDocument::FindBackward);

            if (!prev_c.isNull())
            {
                // all leading characters are \s => shift start search point up
                if (prev_c.selectionEnd() >= c.position())
                {
                    QTextCursor upper(document());
                    upper.setPosition(prev_c.selectionStart());
                    prev_c = document()->find(indent_regex, upper, QTextDocument::FindBackward);
                }

                if (!prev_c.isNull())
                {
                    // remove subsequent \s
                    QTextCursor next_c = document()->find(QRegularExpression("(\\s+)(?=\\S+)"), c);
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
            QTextCursor notSpace = document()->find(QRegularExpression("\\S"), startOfText);
            if (!notSpace.isNull() && notSpace.block() == c.block() && notSpace.position() - 1 > startOfText)
                startOfText = notSpace.position() - 1;
            int nextPos = startOfText;
            if (c.position() <= startOfText && c.position() > c.block().position())
                nextPos = c.block().position();
            else
                nextPos = startOfText;
            c.setPosition(nextPos, keyEvent->modifiers().testFlag(Qt::ShiftModifier) ?
                              QTextCursor::KeepAnchor :
                              QTextCursor::MoveAnchor);
            setTextCursor(c);
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
            else
                break;
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            setTextCursor(c);
            return true;
        }
        else if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            bool cursorToStart = (start == c.position());

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
                        QChar curChar = document()->characterAt(c.position());
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
            if (cursorToStart)
            {
                c.setPosition(end);
                c.setPosition(start < 0 ? 0 : start, QTextCursor::KeepAnchor);
            }
            else
            {
                c.setPosition(start < 0 ? 0 : start);
                c.setPosition(end, QTextCursor::KeepAnchor);
            }
            setTextCursor(c);
            return true;
        }
        break;
    }
    default:
        break;
    }

    return QObject::eventFilter(object, event);
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    // QCompleter cals event() directly, so eventFilter is not called
    if (_completer && _completer->popup()->isVisible())
    {
        switch (e->key())
        {
        case Qt::Key_Enter:
        case Qt::Key_Return:
        case Qt::Key_Escape:
        case Qt::Key_Tab:
        case Qt::Key_Backtab:
            e->ignore();
            return;
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Home:
        case Qt::Key_End:
            _completer->popup()->hide();
            break;
        default:
            break;
        }
    }
    else
    {
        if (e->key() == Qt::Key_M && e->modifiers().testFlag(Qt::ControlModifier))
        {
            QTextCursor c = textCursor();
            CodeBlockProperties *prop = static_cast<CodeBlockProperties*>(c.block().userData());
            c.block().setUserData(prop ? nullptr : new CodeBlockProperties(this));
            // do not prevent further handling of key event to allow left-side panel to be repainted immediately
        }
        else if (e->key() == Qt::Key_Space && e->modifiers().testFlag(Qt::ControlModifier))
        {
            if (!isEnveloped(textCursor().position()))
                emit completerRequest();
            return;
        }
    }

    int prevPos = textCursor().position();
    QPlainTextEdit::keyPressEvent(e);
    int newPos = textCursor().position();
    if (prevPos == newPos)
        return;

    // adjust completer
    if (_completer && _completer->popup()->isVisible())
    {
        auto popup = _completer->popup();
        if (newPos < prevPos)
        {
            if (!_completer->completionPrefix().length())
            {
                popup->hide();
                return;
            }
            int len = _completer->completionPrefix().length() - prevPos + newPos;
            _completer->setCompletionPrefix(_completer->completionPrefix().left(len));
        }
        else
        {
            auto c = textCursor();
            c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, newPos - prevPos);
            _completer->setCompletionPrefix(_completer->completionPrefix() + c.selectedText());
        }
        if (_completer->completionCount())
            popup->selectionModel()->setCurrentIndex(
                        popup->model()->index(0, 0),
                        QItemSelectionModel::SelectCurrent);
        else
            popup->hide();
    }
}

void CodeEditor::insertFromMimeData(const QMimeData *source)
{
    if (!source->hasText())
        return;
    insertPlainText(source->text());

//  should be rethinked  :/
/*

    // count distance from the beginning of the line to the insertion position (chars)
    QTextCursor c = textCursor();
    QString block = c.block().text();
    int distance = 0;
    int tabSize = SqtSettings::value("tabSize", 3).toInt();
    for (int i = 0; i < c.positionInBlock(); ++i)
        distance += (block[i] == '\t' ? tabSize : 1);

    // prepend every line of the inserted text with the following prefix
    QString prefix = QString(distance / tabSize, '\t') + (distance % tabSize ? "\t" : "");
    QString data = source->text();
    if (distance % tabSize)
        data = '\t' + data;
    data.replace('\n', '\n' + prefix);

    // TODO evaluate correct indentation according to surrounding code,
    // find shortest indent within the text being inserted and make corresponding
    // replacements before insertion

    insertPlainText(data);
*/
}

void CodeEditor::updateLeftSideBarWidth()
{
    setViewportMargins(leftSideBarWidth(), 0, 0, 0);
}

void CodeEditor::updateLeftSideBar(const QRect &rect, int dy)
{
    if (dy)
        _leftSideBar->scroll(0, dy);
    else
        _leftSideBar->update(0, rect.y(), _leftSideBar->width(), rect.height());

    if (rect.contains(viewport()->rect()))
        updateLeftSideBarWidth();
}

bool operator == (QTextEdit::ExtraSelection &l, QTextEdit::ExtraSelection &r)
{
    return (l.cursor == r.cursor && l.format == r.format);
}

void CodeEditor::onHlTimerTimeout()
{
    // ------------ match selected word ------------
    QTextCursor curCursor = textCursor();
    QString selectedText = curCursor.selectedText();
    QList<QTextEdit::ExtraSelection> selections;
    QString content;
    if (!selectedText.isEmpty())
    {
        // prevent search for not a word
        bool mayBeWord = true;
        for (const QChar &c: selectedText)
        {
            if (!c.isLetterOrNumber() && c != '_')
            {
                mayBeWord = false;
                break;
            }
        }

        int curPos = curCursor.selectionStart();
        QTextCursor testCursor = textCursor();
        testCursor.setPosition(curPos);
        testCursor.select(QTextCursor::WordUnderCursor);
        if (mayBeWord && selectedText == testCursor.selectedText())
        {
            int pos_backward = testCursor.selectionStart() - 1;
            int pos_forward = testCursor.selectionEnd();
            int forward_counter = 0;
            int backward_counter = 0;
            int total_hits = 0;
            int wordLength = selectedText.length();
            content = text();
            QColor selectionColor(Qt::yellow);
            selectionColor.setAlphaF(0.7);
            QTextEdit::ExtraSelection s;
            s.format.setBackground(selectionColor);

            auto verifyPos = [this, &selections, wordLength, &s, &testCursor, &content, &total_hits](int &pos, int &counter)
            {
                ++total_hits;
                bool left_ok = true;
                bool right_ok = true;
                if (pos)
                {
                    QChar c = content[pos - 1];
                    if (c.isLetterOrNumber() || c == '_')
                        left_ok = false;
                }
                if (left_ok && pos < content.length() - wordLength)
                {
                    QChar c = content[pos + wordLength];
                    if (c.isLetterOrNumber() || c == '_')
                        right_ok = false;
                }

                if (left_ok && right_ok)
                {
                    testCursor.setPosition(pos);
                    testCursor.setPosition(pos + wordLength, QTextCursor::KeepAnchor);
                    s.cursor = testCursor;
                    selections.append(s);
                    ++counter;
                }
            };

            // search current word in both directions
            // * max word matches in every direction: 100
            // * max total matches (including partial): 500
            do
            {
                if (pos_forward >= 0)
                    pos_forward = content.indexOf(selectedText, pos_forward);
                if (pos_backward >= 0)
                    pos_backward = content.lastIndexOf(selectedText, pos_backward);

                if (pos_forward >= 0)
                {
                    verifyPos(pos_forward, forward_counter);
                    if (forward_counter == 100)
                        pos_forward = -1;
                    else
                        pos_forward += wordLength;
                }

                if (pos_backward >= 0)
                {
                    verifyPos(pos_backward, backward_counter);
                    if (backward_counter == 100)
                        pos_backward = -1;
                    else
                        pos_backward -= 1;
                }
            }
            while ((pos_backward >= 0 || pos_forward >= 0) && total_hits < 500);
        }
    }

    // ---------- match brackets ------------

    QList<QTextEdit::ExtraSelection> left_bracket_selections;
    QList<QTextEdit::ExtraSelection> right_bracket_selections;

    QTextCursor cursor = textCursor();
    //if (cursor.selectedText().length() < 2)
    {
        QTextCursor c(cursor);
        c.clearSelection();
        if (c.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor))
        {
            // get bracket to the left and matching one selected
            left_bracket_selections.append(matchBracket(content, c));
            c.movePosition(QTextCursor::NextCharacter);
        }

        if (c.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor))
        {
            // get bracket to the right and matching one selected
            right_bracket_selections.append(matchBracket(content, c, left_bracket_selections.empty() ? 100 : 115));
        }
    }

    selections += right_bracket_selections + left_bracket_selections + currentLineSelection();
    setExtraSelections(selections);
}

void CodeEditor::insertCompletion(const QString &completion)
{
    if (_completer->widget() != this)
        return;
    QTextCursor tc = textCursor();
    int extra = completion.length() - _completer->completionPrefix().length();
    QString text = tc.block().text();
    int bpos = tc.positionInBlock();
    // clear the rest of current word
    while (bpos < text.length())
    {
        QChar c = text[bpos];
        if (!c.isLetterOrNumber() && c != '_')
            break;
        tc.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        ++bpos;
    }
    tc.insertText(completion.right(extra));
}

QList<QTextEdit::ExtraSelection> CodeEditor::matchBracket(QString &docContent, const QTextCursor &selectedBracket, int darkerFactor) const
{
    QList<QTextEdit::ExtraSelection> selections;
    QChar c1 = selectedBracket.selectedText()[0];
    QString brackets("([{)]}");
    int c1pos = brackets.indexOf(c1, Qt::CaseInsensitive);
    // not a bracket or within commented text / string literal / so on
    if (c1pos == -1 || isEnveloped(selectedBracket.selectionStart()))
        return selections;

    // find pair character
    QChar c2 = (c1pos < 3 ? brackets[c1pos + 3] : brackets[c1pos - 3]);

    // initial selection of current bracket
    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QColor(160,255,160).darker(darkerFactor));
    selection.cursor = selectedBracket;
    selections.append(selection);

    if (docContent.isEmpty())
        docContent = text();

    int depth = 1;
    int distance = 0;
    int delta = (c1pos < 3 ? 1 : -1);
    int length = docContent.length();
    int pos = selectedBracket.selectionStart();

    // manual search to speed it up
    do
    {
        pos += delta;
        ++distance;
        // prevent extremely far search
        if (distance > 500000)
        {
            selections.clear();
            return selections;
        }

        if (pos < 0 || pos == length)
            break;
        QCharRef c = docContent[pos];
        if ((c != c1 && c != c2) || isEnveloped(pos))
            continue;

        depth += (c == c1 ? 1 : -1);
    }
    while (depth);

    if (depth) // pair is not matched - change color of initial character
        selections[0].format.setBackground(QColor(255,160,160).darker(darkerFactor));
    else
    {
        selection.cursor = textCursor();
        selection.cursor.setPosition(pos);
        selection.cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        selections.append(selection);
    }

    return selections;
}

QList<QTextEdit::ExtraSelection> CodeEditor::currentLineSelection() const
{
    if (isReadOnly() || !SqtSettings::value("highlightCurrentLine", false).toBool())
        return QList<QTextEdit::ExtraSelection>();

    QTextEdit::ExtraSelection s;
    QColor lineColor(palette().windowText().color());
    lineColor.setAlphaF(0.05);
    s.format.setBackground(lineColor);
    s.format.setProperty(QTextFormat::FullWidthSelection, true);
    s.cursor = textCursor();
    s.cursor.clearSelection();
    return QList<QTextEdit::ExtraSelection> {s};
}

bool CodeEditor::isEnveloped(int pos) const
{
    QTextBlock b = this->document()->findBlock(pos);
    int posInBlock = pos - b.position();
    for (const QTextLayout::FormatRange &r: b.layout()->formats())
    {
        if (posInBlock >= r.start && posInBlock < r.start + r.length)
        {
            if (r.format.property(QTextFormat::UserProperty) == "envelope")
                return true;
            break;
        }
    }
    return false;
}

CodeBlockProperties::CodeBlockProperties(CodeEditor *editor) : QTextBlockUserData(), _editor(editor)
{
    _bookmarks.append(this);
    _lastUsedBookmarkPos = _bookmarks.size() - 1;
}

CodeBlockProperties::~CodeBlockProperties()
{
    _lastUsedBookmarkPos = _bookmarks.indexOf(this);
    _bookmarks.removeOne(this);
    if (!_lastUsedBookmarkPos)
        _lastUsedBookmarkPos = _bookmarks.size() - 0.5f;
    else
        _lastUsedBookmarkPos -= 0.5;
}

CodeEditor *CodeBlockProperties::editor() const
{
    return _editor;
}

CodeBlockProperties *Bookmarks::next()
{
    if (_bookmarks.isEmpty())
        return nullptr;

    if (_bookmarks.size() == 1)
        return _bookmarks[0];

    // use current position (if not deleted) or jump to previous existing one before search next
    _lastUsedBookmarkPos = static_cast<int>(_lastUsedBookmarkPos);

    _lastUsedBookmarkPos = (_bookmarks.size() - 1 > _lastUsedBookmarkPos ?
                                _lastUsedBookmarkPos + 1 : 0);
    return _bookmarks[int(_lastUsedBookmarkPos)];
}

CodeBlockProperties *Bookmarks::previous()
{
    if (_bookmarks.isEmpty())
        return nullptr;

    if (_bookmarks.size() == 1)
        return _bookmarks[0];

    // use current position (if not deleted) or jump to next existing one before search previous
    _lastUsedBookmarkPos = static_cast<int>(_lastUsedBookmarkPos + 0.5f);

    _lastUsedBookmarkPos = (_lastUsedBookmarkPos > 0 ?
                                _lastUsedBookmarkPos - 1 : _bookmarks.size() - 1);
    return _bookmarks[int(_lastUsedBookmarkPos)];
}

CodeBlockProperties *Bookmarks::last()
{
    if (_bookmarks.isEmpty())
        return nullptr;

    if (_lastUsedBookmarkPos != static_cast<int>(_lastUsedBookmarkPos))
        return _bookmarks[_bookmarks.size() - 1];

    return _bookmarks[int(_lastUsedBookmarkPos)];
}
