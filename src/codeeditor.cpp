#include <QTextLayout>
#include <QScrollBar>
#include <utility>
#include <QtGlobal>
#include <QPlainTextDocumentLayout>
#include <QAbstractTextDocumentLayout>
#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include <QDesktopServices>
#include <QCompleter>
#include <QAbstractItemView>
#include <QApplication>
#include <QMouseEvent>
#include "settings.h"
#include "styling.h"
#include <QDebug>
#include <qclipboard.h>

#define RIGHT_MARGIN 2
#define ICON_PLACE_WIDTH 13

static QList<CodeBlockProperties*> _bookmarks;
static QSet<CodeBlockProperties*> _deletedBookmarks;
static float _lastUsedBookmarkPos = 0;
static bool _suspendBookmarks = false;

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

    // blinking caret for every cursor beyond the main one - QPlainTextEdit
    // only ever draws its own single native cursor, so extra carets are
    // painted by us in CodeEditor::paintEvent and need their own blink timer
    _caretBlinkTimer = new QTimer(this);
    int flash = QApplication::cursorFlashTime();
    _caretBlinkTimer->setInterval(flash > 0 ? flash / 2 : 500);
    connect(_caretBlinkTimer, &QTimer::timeout, this, &CodeEditor::onCaretBlink);

    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLeftSideBarWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLeftSideBar);
    connect(this, &CodeEditor::cursorPositionChanged, [this]() {
        _leftSideBar->update();
        auto selections = extraSelections();
        // remove old line indicator
        if (!selections.isEmpty() && selections.back().format.property(QTextFormat::FullWidthSelection).toBool())
            selections.pop_back();
        // add new line indicator (+ multi-cursor highlights)
        setExtraSelections(selections + baseExtraSelections());
        _hlTimer->start();
    });
    connect(this, &CodeEditor::textChanged, [this]() {
        setExtraSelections(baseExtraSelections());
        _hlTimer->start();
    });
    connect(this, &CodeEditor::selectionChanged, _hlTimer, static_cast<void(QTimer::*)(void)>(&QTimer::start));

    _multiCursor.setCursors(textCursor());
    _nativeCursorWidth = cursorWidth(); // remember it before we ever touch it via setCursorWidth(0)

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
                                   top + (bottom - top - imgBookmark.height())/2,
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
            fontMetrics().horizontalAdvance(QString::number(blockCount()));
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

// ---------------------------------------------------------------------
// Multi-cursor plumbing
// ---------------------------------------------------------------------

void CodeEditor::syncFromNativeCursor()
{
    // Something moved/edited via QPlainTextEdit's own machinery (a plain
    // click, or any key we don't give special multi-cursor treatment to,
    // e.g. Ctrl+A/C/V/X/Z/Y) - that only ever touches the single native
    // cursor, so we drop back to single-cursor mode to match reality.
    _multiCursor.setCursors(textCursor());
    _caretBlinkTimer->stop();
    _caretBlinkVisible = true;
    setCursorWidth(_nativeCursorWidth);
    // Back to a single, natively-driven cursor: hand overwrite mode back
    // to Qt so it resumes drawing/blinking its own block caret correctly.
    setOverwriteMode(_overwriteMode);
    viewport()->update(); // clear any extra carets we painted ourselves
}

void CodeEditor::syncToNativeCursor()
{
    setTextCursor(_multiCursor.mainCursor());
    if (_multiCursor.isMultiple())
    {
        // Hide Qt's own blinking caret and draw every cursor - including
        // the main one - ourselves in paintEvent(), all off the same
        // timer/flag. Otherwise the native caret keeps its own blink
        // cycle (reset on every setTextCursor call) and drifts out of
        // phase with the carets we paint for the other cursors.
        setCursorWidth(0);
        // Qt's overwrite caret ignores cursorWidth(0) and draws itself
        // anyway, on its own blink cycle - so with several cursors, take
        // overwrite fully into our own hands and keep Qt's flag off.
        setOverwriteMode(false);
        if (!_caretBlinkTimer->isActive())
            _caretBlinkTimer->start();
    }
    else
    {
        setCursorWidth(_nativeCursorWidth);
        setOverwriteMode(_overwriteMode);
        _caretBlinkTimer->stop();
        _caretBlinkVisible = true;
    }
    viewport()->update();
}

void CodeEditor::collapseToSingleCursor()
{
    if (!_multiCursor.isMultiple())
        return;
    QTextCursor c = _multiCursor.mainCursor();
    c.clearSelection();
    _multiCursor.setCursors(c);
    setTextCursor(c);
    setCursorWidth(_nativeCursorWidth);
    setOverwriteMode(_overwriteMode);
    _caretBlinkTimer->stop();
    _caretBlinkVisible = true;
    viewport()->update();
}

void CodeEditor::onCaretBlink()
{
    _caretBlinkVisible = !_caretBlinkVisible;
    viewport()->update();
}

void CodeEditor::resetCaretBlink()
{
    if (!_caretBlinkTimer->isActive())
        return; // single cursor: Qt handles its own caret/blinking, nothing to reset here

    if (!_caretBlinkVisible)
    {
        _caretBlinkVisible = true;
        viewport()->update();
    }
    _caretBlinkTimer->start(); // restart the countdown so it doesn't blink again until idle
}

bool CodeEditor::isNavigationKey(int key)
{
    switch (key)
    {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
        return true;
    default:
        return false;
    }
}

QTextCursor::MoveOperation CodeEditor::moveOperationForKey(int key, bool ctrl)
{
    switch (key)
    {
    case Qt::Key_Left:  return ctrl ? QTextCursor::NoMove : QTextCursor::Left;  // ctrl case handled separately, see nextWordBoundary/previousWordBoundary
    case Qt::Key_Right: return ctrl ? QTextCursor::NoMove : QTextCursor::Right; // ditto
    case Qt::Key_Up:    return QTextCursor::Up;
    case Qt::Key_Down:  return QTextCursor::Down;
    case Qt::Key_Home:  return ctrl ? QTextCursor::Start : QTextCursor::StartOfLine;
    case Qt::Key_End:   return ctrl ? QTextCursor::End   : QTextCursor::EndOfLine;
    default:            return QTextCursor::NoMove;
    }
}

namespace
{
    enum class WordCharClass { Space, Word, Other };

    WordCharClass classifyWordChar(QChar ch)
    {
        // QChar::isSpace() already covers QChar::ParagraphSeparator (the
        // character QTextDocument uses between blocks), so word jumps cross
        // line boundaries the same way Qt's own NextWord/PreviousWord do.
        if (ch.isSpace())
            return WordCharClass::Space;
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
            return WordCharClass::Word;
        return WordCharClass::Other;
    }
}

int CodeEditor::nextWordBoundary(QTextDocument *doc, int pos)
{
    // characterCount() counts one extra (implicit, non-printable) trailing
    // character; the last real, addressable position is one before it.
    const int end = doc->characterCount() - 1;
    if (pos >= end)
        return end;

    // Swallow whitespace on the way, but never stop inside it.
    while (pos < end && classifyWordChar(doc->characterAt(pos)) == WordCharClass::Space)
        ++pos;
    if (pos >= end)
        return end;

    // Then cross exactly one run of same-class characters (a word, or a
    // punctuation/operator run) and stop right where it ends.
    WordCharClass cls = classifyWordChar(doc->characterAt(pos));
    int runEnd = pos;
    while (runEnd < end && classifyWordChar(doc->characterAt(runEnd)) == cls)
        ++runEnd;

    // Exception: a single separator glued directly onto a following word,
    // with no whitespace in between - the '.' in "qwe.rty", the '=' in
    // "a=b" - isn't its own stop. VS Code treats it as glue and jumps
    // straight through it into that word (so "qwe.rty" is two Ctrl+Right
    // presses, not three). A *run* of separators (e.g. "..."), or one
    // bounded by whitespace on the far side (e.g. "foo = bar"), still gets
    // its own stop same as before - only a lone, word-adjacent one merges.
    if (cls == WordCharClass::Other && runEnd - pos == 1 &&
        runEnd < end && classifyWordChar(doc->characterAt(runEnd)) == WordCharClass::Word)
    {
        int wordEnd = runEnd;
        while (wordEnd < end && classifyWordChar(doc->characterAt(wordEnd)) == WordCharClass::Word)
            ++wordEnd;
        return wordEnd;
    }

    return runEnd;
}

int CodeEditor::previousWordBoundary(QTextDocument *doc, int pos)
{
    if (pos <= 0)
        return 0;

    int p = pos - 1;
    while (p > 0 && classifyWordChar(doc->characterAt(p)) == WordCharClass::Space)
        --p;
    if (classifyWordChar(doc->characterAt(p)) == WordCharClass::Space)
        return 0; // ran into the very start of the document, still in whitespace

    WordCharClass cls = classifyWordChar(doc->characterAt(p));
    int runStart = p;
    while (runStart > 0 && classifyWordChar(doc->characterAt(runStart - 1)) == cls)
        --runStart;

    // Mirror of the forward exception above: a lone separator glued
    // directly onto a preceding word is glue, not its own stop.
    if (cls == WordCharClass::Other && p == runStart &&
        runStart > 0 && classifyWordChar(doc->characterAt(runStart - 1)) == WordCharClass::Word)
    {
        int wordStart = runStart;
        while (wordStart > 0 && classifyWordChar(doc->characterAt(wordStart - 1)) == WordCharClass::Word)
            --wordStart;
        return wordStart;
    }

    return runStart;
}

// Ctrl+D: select the word under the (main) cursor, or - if something is
// already selected - add a new cursor on the next occurrence of that
// selection further down in the document (wrapping around at the end).
void CodeEditor::selectNextOccurrence()
{
    QTextCursor main = _multiCursor.mainCursor();
    QTextCursor searchFrom = main;
    QString needle;

    if (main.hasSelection())
    {
        needle = main.selectedText();
    }
    else
    {
        QTextCursor word = main;
        word.select(QTextCursor::WordUnderCursor);
        if (word.selectedText().isEmpty())
            return;

        if (!_multiCursor.isMultiple())
        {
            // first Ctrl+D on a bare cursor just selects the word, like
            // most editors do - the next press adds a cursor
            _multiCursor.setCursors(word);
            syncToNativeCursor();
            return;
        }

        needle = word.selectedText();
        searchFrom = word;
    }

    if (needle.isEmpty())
        return;

    QTextCursor found = document()->find(needle, searchFrom.selectionEnd(), QTextDocument::FindCaseSensitively);
    if (found.isNull())
        found = document()->find(needle, 0, QTextDocument::FindCaseSensitively);
    if (found.isNull())
        return;

    _multiCursor.addCursor(found);
    syncToNativeCursor();
    ensureCursorVisible();
}

// Ctrl+Alt+Up / Ctrl+Alt+Down: add a cursor directly above/below the main
// one, at the same visual column (clamped to the target line's length).
void CodeEditor::addCursorOnAdjacentLine(bool below)
{
    QTextCursor main = _multiCursor.mainCursor();
    QTextBlock block = main.block();
    QTextBlock target = below ? block.next() : block.previous();
    if (!target.isValid())
        return;

    QTextDocument *doc = main.document();
    int indent = indentSize();

    // A raw character count isn't the same thing as a screen column once
    // tabs are involved: a tab is one character but several columns wide,
    // and the two lines can be indented with a different number of tabs.
    // Using the character count directly landed the new cursor at the
    // wrong visual position (and even inside the indentation) whenever
    // that differed - so expand tabs (see visualColumnAt()) and work in
    // visual columns instead.
    int wantedCol = visualColumnAt(doc, block.position(), main.position(), indent);
    int targetLen = target.length() > 0 ? target.length() - 1 : 0; // exclude the block's paragraph separator

    // Walk the target line, expanding tabs the same way, until its visual
    // column reaches (or would pass) the wanted one.
    int offset = 0;
    int col = 0;
    while (offset < targetLen && col < wantedCol)
    {
        col += (doc->characterAt(target.position() + offset) == '\t') ? indent - (col % indent) : 1;
        ++offset;
    }

    QTextCursor c(target);
    c.setPosition(target.position() + offset);

    _multiCursor.addCursor(c);
    syncToNativeCursor();
    ensureCursorVisible();
}

// ---- per-cursor editing primitives -----------------------------------
// Each of these takes the cursor it operates on by reference. They are
// used exactly the same way whether there is one active cursor (called
// once) or several (called once per cursor from a MultiTextCursor loop).

int CodeEditor::indentSize() const
{
    return SqtSettings::value("indentSize", 3).toInt();
}

// Visual column of `pos` within its block, counting from `blockStart`,
// expanding tabs to the next multiple of indentSize - shared by every
// place that needs to reason about indentation in screen columns rather
// than raw character counts (a tab is one character but several columns).
int CodeEditor::visualColumnAt(QTextDocument *doc, int blockStart, int pos, int indentSize)
{
    int col = 0;
    for (int p = blockStart; p < pos; ++p)
        col += (doc->characterAt(p) == '\t') ? indentSize - (col % indentSize) : 1;
    return col;
}

void CodeEditor::applySingleCursorEdit(QTextCursor &c)
{
    setTextCursor(c);
    _multiCursor.setCursors(c);
    _multiUndoHistory.clear();
    _multiRedoHistory.clear();
}

bool CodeEditor::applySmartBackspace(QTextCursor &c)
{
    if (c.hasSelection())
        return false;

    QTextDocument *doc = c.document();
    int indent = indentSize();
    int pos = c.position();
    int blockStart = c.block().position();

    int col = visualColumnAt(doc, blockStart, pos, indent);

    int prevBoundary;
    if (col % indent == 0 && col > 0)
        prevBoundary = col - indent;
    else
        prevBoundary = (col / indent) * indent;

    int toRemove = col - prevBoundary;
    if (toRemove == 0)
        return false;

    int spacesBefore = 0;
    QTextCursor back(c);
    back.setPosition(pos);
    while (back.position() > blockStart)
    {
        back.movePosition(QTextCursor::PreviousCharacter);
        QChar ch = doc->characterAt(back.position());
        if (ch == ' ')
            spacesBefore++;
        else
            break;
    }

    if (spacesBefore >= toRemove)
    {
        // delete toRemove spaces before the cursor
        c.setPosition(pos - toRemove);
        c.setPosition(pos, QTextCursor::KeepAnchor);
        c.removeSelectedText();
        return true;
    }

    // lack of spaces - caller should fall back to a plain single-char delete
    return false;
}

void CodeEditor::applyReturnWithIndent(QTextCursor &c)
{
    QTextDocument *doc = c.document();

    // previous indentation
    c.removeSelectedText();
    QRegularExpression indentRegex("(^\\s*)(?=[^\\s\\r\\n]+)");
    QTextCursor prevC = doc->find(indentRegex, c, QTextDocument::FindBackward);

    if (!prevC.isNull())
    {
        // all leading characters are \s => shift start search point up
        if (prevC.selectionEnd() >= c.position())
        {
            QTextCursor upper(doc);
            upper.setPosition(prevC.selectionStart());
            prevC = doc->find(indentRegex, upper, QTextDocument::FindBackward);
        }

        if (!prevC.isNull())
        {
            // remove subsequent \s
            QTextCursor nextC = doc->find(QRegularExpression("(\\s+)(?=\\S+)"), c);
            if (nextC.selectionStart() == c.selectionStart())
                nextC.removeSelectedText();
        }
    }

    // insert indentation
    c.insertText("\n" + prevC.selectedText());
}

void CodeEditor::applyHome(QTextCursor &c, bool keepAnchor)
{
    QTextDocument *doc = c.document();
    int startOfText = c.block().position();
    QTextCursor notSpace = doc->find(QRegularExpression("\\S"), startOfText);
    if (!notSpace.isNull() && notSpace.block() == c.block() && notSpace.position() - 1 > startOfText)
        startOfText = notSpace.position() - 1;
    int nextPos = startOfText;
    if (c.position() <= startOfText && c.position() > c.block().position())
        nextPos = c.block().position();
    c.setPosition(nextPos, keepAnchor ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor);
}

void CodeEditor::applyMultiLineIndent(QTextCursor &c, bool forward)
{
    int start = c.selectionStart();
    int end = c.selectionEnd();
    bool cursorToStart = (start == c.position());

    // Which blocks (lines) are affected: every line the selection touches
    // at all, or - with no selection - just the line the caret is on.
    // Using block numbers rather than raw positions makes the boundary
    // unambiguous: a (non-empty) selection that ends exactly at the start
    // of a line, with nothing of that line actually highlighted, does not
    // pull that line in.
    QTextCursor endCur(c);
    endCur.setPosition(end);
    int endBlockNumber = endCur.blockNumber();
    if (end > start && endCur.atBlockStart())
        --endBlockNumber;

    c.setPosition(start);
    c.movePosition(QTextCursor::StartOfBlock);
    c.beginEditBlock();
    int indentW = indentSize();

    if (!forward) // Backtab / Shift+Tab
    {
        QTextDocument *doc = c.document();

        while (true)
        {
            if (c.blockNumber() > endBlockNumber)
                break;

            // Walk the line's leading run of spaces/tabs from its start,
            // recording each character's *visual* width (a tab advances to
            // the next tab stop, so its width depends on what came before
            // it) so we know both the total indent width and, later, which
            // characters to drop from the end of that run.
            int lineStart = c.position();
            QVector<int> charWidths;
            int width = 0;
            int scanPos = lineStart;
            while (true)
            {
                QChar ch = doc->characterAt(scanPos);
                if (ch == QLatin1Char(' '))
                {
                    charWidths.append(1);
                    width += 1;
                }
                else if (ch == QLatin1Char('\t'))
                {
                    int w = indentW - (width % indentW);
                    charWidths.append(w);
                    width += w;
                }
                else
                    break;
                ++scanPos;
            }
            int indentEnd = scanPos; // one past the last space/tab, i.e. where real text starts

            if (width > 0)
            {
                // Target: the previous tab stop below the current width -
                // e.g. 5 columns of indent with indentW=4 snaps to 4, not
                // to 1 ("remove up to indentW characters"). Remove just
                // enough characters to get there, taken from the *end* of
                // the indent run (the side touching the real text), not
                // the start - so 5 spaces loses the 5th one, and
                // "\t " (tab + one space, width 4+1=5 with indentW=4)
                // loses that trailing space, not the tab.
                int targetWidth = ((width - 1) / indentW) * indentW;
                int toRemove = width - targetWidth;

                int accumulated = 0;
                int removeCount = 0;
                for (int i = charWidths.size() - 1; i >= 0; --i)
                {
                    accumulated += charWidths[i];
                    ++removeCount;
                    if (accumulated >= toRemove)
                        break;
                }

                int removeStart = indentEnd - removeCount;
                c.setPosition(removeStart);
                c.setPosition(indentEnd, QTextCursor::KeepAnchor);
                c.removeSelectedText();

                // Keep start/end (and hence the cursor/selection restored
                // at the end of the function) pinned to the same real
                // character they pointed at before this line's removal:
                // positions before the removed span don't move, positions
                // after it shift left by however much was removed, and any
                // position that was inside the removed span (only possible
                // for a selection edge sitting inside this line's own
                // indent) collapses to the start of that span.
                auto adjust = [&](int pos) {
                    if (pos <= removeStart)
                        return pos;
                    if (pos >= indentEnd)
                        return pos - removeCount;
                    return removeStart;
                };
                start = adjust(start);
                end = adjust(end);
            }

            if (!c.movePosition(QTextCursor::NextBlock))
                break;
        }
    }
    else // Tab
    {
        QTextDocument *doc = c.document();
        bool useTabs = SqtSettings::value("tabsIndent", true).toBool();

        while (true)
        {
            if (c.blockNumber() > endBlockNumber)
                break;

            // Same tab-aware width scan as Backtab, to find this line's
            // current indent width - so we know which tab stop is next.
            int lineStart = c.position();
            int width = 0;
            int scanPos = lineStart;
            while (true)
            {
                QChar ch = doc->characterAt(scanPos);
                if (ch == QLatin1Char(' '))
                    width += 1;
                else if (ch == QLatin1Char('\t'))
                    width += indentW - (width % indentW);
                else
                    break;
                ++scanPos;
            }
            int indentEnd = scanPos;

            // Target: the next tab stop above the current width - always a
            // full step forward, even when already sitting exactly on one
            // (VS Code: a line already at column 4 with indentW=4 lands on
            // 8, not stays put). The whole existing indent run is replaced
            // by a canonical one of that width, rather than just having
            // something appended to it - so mixed leftover spacing from
            // earlier edits doesn't accumulate, and a line's indent always
            // ends up an exact, clean multiple of the tab stop.
            int targetWidth = ((width / indentW) + 1) * indentW;
            QString newIndent = useTabs ? QString(targetWidth / indentW, QLatin1Char('\t'))
                                         : QString(targetWidth, QLatin1Char(' '));

            c.setPosition(lineStart);
            c.setPosition(indentEnd, QTextCursor::KeepAnchor);
            c.insertText(newIndent); // replaces the old indent (the active selection) in one go

            int delta = newIndent.length() - (indentEnd - lineStart);
            auto adjust = [&](int pos) {
                if (pos <= lineStart)
                    return pos;
                if (pos >= indentEnd)
                    return pos + delta;
                return lineStart + int(newIndent.length()); // was inside the old indent run
            };
            start = adjust(start);
            end = adjust(end);

            if (!c.movePosition(QTextCursor::NextBlock))
                break;
        }
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
}

void CodeEditor::applySingleLineTab(QTextCursor &c)
{
    int indent = indentSize();
    bool useTabs = SqtSettings::value("tabsIndent", true).toBool();

    if (c.hasSelection())
        c.removeSelectedText();

    QTextDocument *doc = c.document();
    int pos = c.position();
    int blockStart = c.block().position();

    int col = visualColumnAt(doc, blockStart, pos, indent);

    int targetCol = ((col / indent) + 1) * indent;
    int spacesToAdd = targetCol - col;
    if (spacesToAdd == 0)
        spacesToAdd = indent;

    QString insertText = useTabs ? "\t" : QString(spacesToAdd, ' ');
    c.insertText(insertText);
}

// ---- multi-cursor-aware undo/redo -------------------------------------

QVector<QPair<int,int>> CodeEditor::snapshotCursors(const MultiTextCursor &mc)
{
    QVector<QPair<int,int>> result;
    for (const QTextCursor &c : mc.cursors())
        result.append(qMakePair(c.anchor(), c.position()));
    return result;
}

void CodeEditor::restoreCursorSnapshot(const QVector<QPair<int,int>> &snapshot)
{
    if (snapshot.isEmpty())
        return;

    auto makeCursor = [this](const QPair<int,int> &p) {
        QTextCursor c(document());
        c.setPosition(p.first);
        c.setPosition(p.second, QTextCursor::KeepAnchor);
        return c;
    };

    _multiCursor.setCursors(makeCursor(snapshot.first()));
    for (int i = 1; i < snapshot.size(); ++i)
        _multiCursor.addCursor(makeCursor(snapshot[i]));
}

void CodeEditor::recordMultiEditUndo(const QVector<QPair<int,int>> &preState)
{
    MultiEditCursorHistory entry;
    entry.pre = preState;
    entry.post = snapshotCursors(_multiCursor);

    _multiUndoHistory.append(entry);
    _multiRedoHistory.clear();
}

// Performs an edit through every active cursor (see MultiTextCursor::editBlock)
// and remembers the cursor set from just before/after it, so consecutive
// Ctrl+Z / Ctrl+Y operations restore the whole set in lockstep.
void CodeEditor::performMultiEdit(std::function<void(QTextCursor&)> fn)
{
    QVector<QPair<int,int>> preState = snapshotCursors(_multiCursor);
    _multiCursor.editBlock(fn);
    syncToNativeCursor();
    recordMultiEditUndo(preState);
}

// Indices into _multiCursor.cursors(), ascending by selectionStart() - the
// document order in which several cursors' text should be joined/consumed
// (copy/cut, and distributing pasted lines one-per-cursor).
QVector<int> CodeEditor::cursorsOrderedByPosition() const
{
    QVector<int> order(_multiCursor.count());
    for (int i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [this](int a, int b) {
        return _multiCursor.cursors()[a].selectionStart() < _multiCursor.cursors()[b].selectionStart();
    });
    return order;
}

// Every cursor's selected text, in document order, joined by '\n' - used
// for Ctrl+C / Ctrl+X with several cursors. QTextCursor::selectedText()
// uses U+2029 (paragraph separator) instead of '\n' for multi-line
// selections, so that gets normalized too.
QString CodeEditor::multiCursorSelectedText() const
{
    QStringList parts;
    for (int i : cursorsOrderedByPosition())
    {
        const QTextCursor &c = _multiCursor.cursors()[i];
        if (c.hasSelection())
            parts << QString(c.selectedText()).replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    }
    return parts.join(QLatin1Char('\n'));
}

// The single place that decides what a copy (or cut, or drag) out of this
// editor yields. Overriding it - instead of only special-casing Ctrl+C -
// means *every* copy path goes through the multi-cursor logic, including
// QPlainTextEdit's own copy()/cut() and any menu action bound to them.
QMimeData *CodeEditor::createMimeDataFromSelection() const
{
    if (!_multiCursor.isMultiple())
        return QPlainTextEdit::createMimeDataFromSelection();

    QString joined = multiCursorSelectedText();
    if (joined.isEmpty()) // several carets, but nothing actually selected
        return QPlainTextEdit::createMimeDataFromSelection();

    QMimeData *result = new QMimeData;
    result->setText(joined);
    return result;
}

// Copies the current selection(s) to the clipboard. Leaves the clipboard
// alone (and reports false) when there is nothing selected at all, so an
// accidental Ctrl+C on a bare caret doesn't wipe what's already there.
bool CodeEditor::copySelectionToClipboard() const
{
    QMimeData *data = createMimeDataFromSelection();
    if (!data)
        return false;
    if (data->text().isEmpty())
    {
        delete data;
        return false;
    }
    QApplication::clipboard()->setMimeData(data);
    return true;
}

// ---------------------------------------------------------------------

// Whether this key event is the Copy shortcut (Ctrl+C or Ctrl+Insert).
// Shared between the ShortcutOverride and KeyPress handling below so the
// two can never drift out of sync with each other.
static bool isCopyShortcut(const QKeyEvent *e)
{
    if (e->matches(QKeySequence::Copy))
        return true;
    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = e->modifiers().testFlag(Qt::AltModifier);
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    return ctrl && !alt && !shift && e->key() == Qt::Key_Insert;
}

// Whether this key event is the Cut shortcut (Ctrl+X or Shift+Delete).
static bool isCutShortcut(const QKeyEvent *e)
{
    if (e->matches(QKeySequence::Cut))
        return true;
    const bool ctrl = e->modifiers().testFlag(Qt::ControlModifier);
    const bool alt = e->modifiers().testFlag(Qt::AltModifier);
    const bool shift = e->modifiers().testFlag(Qt::ShiftModifier);
    return shift && !ctrl && !alt && e->key() == Qt::Key_Delete;
}

bool CodeEditor::eventFilter(QObject *object, QEvent *event)
{
    switch (event->type())
    {
    case QEvent::FontChange:
        _leftSideBar->setFont(font());
        updateLeftSideBarWidth();
        break;
    case QEvent::ShortcutOverride:
    {
        // Qt asks the focused widget "is this yours?" via ShortcutOverride
        // before delivering the matching KeyPress - and before falling
        // back to any *global* shortcut bound to the same key sequence
        // (e.g. an Edit > Copy menu action). QPlainTextEdit's own internal
        // handling only claims Copy/Cut here when the single *native*
        // QTextCursor has a selection - but with several active cursors
        // that native cursor (whichever one is "main") can easily be a
        // plain caret while other cursors hold the real selections. Qt
        // then declines the override, the key sequence gets treated as a
        // global shortcut instead, and our KeyPress handling below never
        // even runs. Claim it ourselves whenever several cursors are
        // active so Ctrl+C / Ctrl+Insert / Ctrl+X / Shift+Delete actually
        // reach us.
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (_multiCursor.isMultiple() && (isCopyShortcut(keyEvent) || isCutShortcut(keyEvent)))
        {
            event->accept();
            return true;
        }
        break;
    }
    case QEvent::KeyPress:
    {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

        // Same reasoning as the call at the top of keyPressEvent(): keep the
        // caret(s) solid during keyboard activity. Needed here too because
        // multi-cursor navigation/creation (arrows, Ctrl+Alt+Up/Down, Ins,
        // etc.) is handled entirely in this filter and returns true before
        // the event ever reaches keyPressEvent().
        resetCaretBlink();

        if (keyEvent->key() == Qt::Key_Escape && _multiCursor.isMultiple())
        {
            collapseToSingleCursor();
            return true;
        }

        if (keyEvent->key() == Qt::Key_F1)
        {
            QString url = SqtSettings::value(keyEvent->modifiers().testFlag(Qt::ShiftModifier) ?
                                                 "shiftF1url" : "f1url").toString();
            QDesktopServices::openUrl(QUrl(url));
            return true;
        }

        // ---- Copy with several cursors: copy every selection, joined by
        // newlines in document order (like VS Code), instead of just the
        // main cursor's selection. Works even in read-only mode, same as a
        // normal copy would. ----
        if (isCopyShortcut(keyEvent) && _multiCursor.isMultiple())
        {
            copySelectionToClipboard();
            return true;
        }

        if (isReadOnly())
            break;

        bool ctrl = keyEvent->modifiers().testFlag(Qt::ControlModifier);
        bool alt = keyEvent->modifiers().testFlag(Qt::AltModifier);
        bool shift = keyEvent->modifiers().testFlag(Qt::ShiftModifier);

        if ((keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) &&
                ((alt && shift && !ctrl) ||     // Alt+Shift+Up/Down, VS Code's default
                 (ctrl && shift && !alt)))      // Ctrl+Shift+Up/Down, VS Code's other default
        {
            addCursorOnAdjacentLine(keyEvent->key() == Qt::Key_Down);
            return true;
        }

        if (ctrl && !alt && keyEvent->key() == Qt::Key_D)
        {
            selectNextOccurrence();
            return true;
        }

        // ---- Cut with several cursors: same idea as copy above, but also
        // removes each selection. ----
        if (isCutShortcut(keyEvent) && _multiCursor.isMultiple())
        {
            copySelectionToClipboard();

            performMultiEdit([](QTextCursor &cur) {
                if (cur.hasSelection())
                    cur.removeSelectedText();
            });
            return true;
        }

        // ---- multi-cursor-aware undo/redo ----
        // Keep consuming Ctrl+Z while our parallel history has entries, so
        // several consecutive undos restore the corresponding cursor sets
        // instead of letting QPlainTextEdit collapse back to one cursor.
        if (ctrl && !alt && !shift && keyEvent->key() == Qt::Key_Z &&
            !_multiUndoHistory.isEmpty())
        {
            const MultiEditCursorHistory entry = _multiUndoHistory.takeLast();
            undo();

            restoreCursorSnapshot(entry.pre);
            syncToNativeCursor();

            _multiRedoHistory.append(entry);
            return true;
        }

        if (ctrl && !alt &&
                ((keyEvent->key() == Qt::Key_Y) || (shift && keyEvent->key() == Qt::Key_Z)) &&
                !_multiRedoHistory.isEmpty())
        {
            const MultiEditCursorHistory entry = _multiRedoHistory.takeLast();
            redo();

            restoreCursorSnapshot(entry.post);
            syncToNativeCursor();

            _multiUndoHistory.append(entry);
            return true;
        }

        if (keyEvent->key() == Qt::Key_Insert && keyEvent->modifiers() == Qt::NoModifier)
        {
            _overwriteMode = !_overwriteMode;
            // With one cursor, hand it straight to Qt - its own overwrite
            // handling and block-caret painting are correct and there's
            // nothing else on screen to clash with. With several cursors
            // the native flag stays off; see the comment by _overwriteMode.
            setOverwriteMode(_multiCursor.isMultiple() ? false : _overwriteMode);
            viewport()->update();
            return true;
        }

        // ---- Ctrl(+Shift)+Left/Right: word jump, VS Code style (see
        // nextWordBoundary/previousWordBoundary). Intercepted unconditionally,
        // single cursor included - Qt's own Ctrl+Left/Right (which we'd
        // otherwise fall through to for a single cursor) pulls the trailing
        // space into the selection, which is exactly what this replaces. ----
        if (ctrl && !alt && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right))
        {
            QTextCursor::MoveMode mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
            bool forward = keyEvent->key() == Qt::Key_Right;
            QTextDocument *doc = document();

            auto moveWord = [doc, forward, mode](QTextCursor &cur) {
                int newPos = forward ? nextWordBoundary(doc, cur.position())
                                      : previousWordBoundary(doc, cur.position());
                cur.setPosition(newPos, mode);
            };

            if (_multiCursor.isMultiple())
            {
                _multiCursor.forEachCursor(moveWord);
                syncToNativeCursor();
            }
            else
            {
                QTextCursor cur = textCursor();
                moveWord(cur);
                setTextCursor(cur);
            }
            return true;
        }

        // ---- navigation: only intercepted here when there is more than
        // one cursor. A single cursor still gets native, battle-tested
        // navigation from QPlainTextEdit further down the pipeline. ----
        if (_multiCursor.isMultiple() && isNavigationKey(keyEvent->key()) && keyEvent->key() != Qt::Key_Home)
        {
            QTextCursor::MoveMode mode = shift ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;

            if (keyEvent->key() == Qt::Key_PageUp || keyEvent->key() == Qt::Key_PageDown)
            {
                int page = qMax(1, viewport()->height() / qMax(1, fontMetrics().height()));
                QTextCursor::MoveOperation op = (keyEvent->key() == Qt::Key_PageUp) ? QTextCursor::Up : QTextCursor::Down;
                _multiCursor.forEachCursor([mode, op, page](QTextCursor &cur) { cur.movePosition(op, mode, page); });
            }
            else
            {
                QTextCursor::MoveOperation op = moveOperationForKey(keyEvent->key(), ctrl);
                _multiCursor.forEachCursor([op, mode](QTextCursor &cur) { cur.movePosition(op, mode); });
            }
            syncToNativeCursor();
            return true;
        }

        // ---- smart Backspace (indent-aware) ----
        if (keyEvent->key() == Qt::Key_Backspace)
        {
            if (_multiCursor.isMultiple())
            {
                performMultiEdit([this](QTextCursor &cur) {
                    if (cur.hasSelection())
                        cur.removeSelectedText();
                    else if (!applySmartBackspace(cur))
                        cur.deletePreviousChar();
                });
                return true;
            }

            QTextCursor c = textCursor();
            if (!c.hasSelection() && applySmartBackspace(c))
            {
                applySingleCursorEdit(c);
                return true;
            }
            // no selection to fall back to, or a plain single-char delete
            // is enough (lack of spaces to remove) - let the default,
            // native Backspace handler take it from here
            break;
        }

        // ---- Delete: only needs custom handling with several cursors;
        // a single cursor already gets a correct Delete from Qt. ----
        if (keyEvent->key() == Qt::Key_Delete && _multiCursor.isMultiple())
        {
            performMultiEdit([](QTextCursor &cur) {
                if (cur.hasSelection())
                    cur.removeSelectedText();
                else
                    cur.deleteChar();
            });
            return true;
        }

        // ---- Return with auto-indent ----
        if (keyEvent->key() == Qt::Key_Return)
        {
            if (_multiCursor.isMultiple())
            {
                performMultiEdit([this](QTextCursor &cur) { applyReturnWithIndent(cur); });
                return true;
            }

            QTextCursor c = textCursor();
            applyReturnWithIndent(c);
            applySingleCursorEdit(c);
            return true;
        }

        // ---- smart Home ----
        if (keyEvent->key() == Qt::Key_Home && !ctrl)
        {
            if (_multiCursor.isMultiple())
            {
                _multiCursor.forEachCursor([this, shift](QTextCursor &cur) { applyHome(cur, shift); });
                syncToNativeCursor();
                return true;
            }

            QTextCursor c = textCursor();
            applyHome(c, shift);
            setTextCursor(c);
            _multiCursor.setCursors(c);
            return true;
        }

        // ---- Tab / Backtab indentation ----
        if (keyEvent->key() == Qt::Key_Tab || keyEvent->key() == Qt::Key_Backtab)
        {
            bool forward = (keyEvent->key() == Qt::Key_Tab);
            auto applyOne = [this, forward](QTextCursor &cur)
            {
                if (!forward)
                {
                    // Shift+Tab always outdents by line, no matter the
                    // selection's shape - a bare caret with no selection
                    // outdents its own line, a same-line selection outdents
                    // that one line, a multi-line selection outdents every
                    // line it touches. Unlike Tab, there's no separate
                    // "replace the selection" behavior to special-case.
                    applyMultiLineIndent(cur, false);
                    return;
                }

                int start = cur.selectionStart();
                int end = cur.selectionEnd();
                cur.setPosition(end);
                int lastBlock = cur.blockNumber();
                cur.setPosition(start);
                bool multiLine = (cur.blockNumber() != lastBlock);
                cur.setPosition(start);
                cur.setPosition(end, QTextCursor::KeepAnchor);

                if (multiLine)
                    applyMultiLineIndent(cur, forward);
                else
                    applySingleLineTab(cur);
            };

            if (_multiCursor.isMultiple())
            {
                performMultiEdit([&applyOne](QTextCursor &cur) { applyOne(cur); });
                return true;
            }

            QTextCursor c = textCursor();
            applyOne(c);
            applySingleCursorEdit(c);
            return true;
        }

        // ---- plain typed text: only needs custom handling with several
        // cursors - a single cursor is typed into natively below. ----
        if (_multiCursor.isMultiple() && !ctrl && !alt && !keyEvent->modifiers().testFlag(Qt::MetaModifier))
        {
            QString t = keyEvent->text();
            if (!t.isEmpty() && t.at(0).isPrint())
            {
                performMultiEdit([t, this](QTextCursor &cur) {
                    if (cur.hasSelection())
                        cur.removeSelectedText();
                    else if (_overwriteMode && !cur.atBlockEnd())
                        cur.deleteChar();
                    cur.insertText(t);
                });
                return true;
            }
        }

        if (_multiCursor.isMultiple())
            break; // any other key: let it fall through, acting on the main cursor only

        QTextCursor c = textCursor();
        if (!c.hasSelection())
            break;

        int start = c.selectionStart();
        int end = c.selectionEnd();

        if (keyEvent->key() == Qt::Key_U)
        {
            if (ctrl)
            {
                if (shift ||
                        // Ubuntu uses Ctrl+Shift+U to enter character by unicode character number,
                        // so we may use Ctrl+Win+U to lowercase selection
                        keyEvent->modifiers().testFlag(Qt::MetaModifier))
                    c.insertText(c.selectedText().toLower());
                else
                    c.insertText(c.selectedText().toUpper());
            }
            else
                break;
            c.setPosition(start);
            c.setPosition(end, QTextCursor::KeepAnchor);
            applySingleCursorEdit(c);
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
    // Keep the caret(s) solid for the duration of keyboard activity (typing,
    // navigation, deletion, autorepeat included) and only let them resume
    // blinking once input goes idle - see resetCaretBlink().
    resetCaretBlink();

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
            if (!_multiCursor.isMultiple() && !isEnveloped(textCursor().position()))
                emit completerRequest();
            return;
        }
        else if (e->key() == Qt::Key_F4 && !e->modifiers().testFlag(Qt::AltModifier))
        {
            emit scriptObjectRequest();
            return;
        }
    }

    int prevRevision = document()->revision();
    QTextCursor prevCursor = textCursor();
    int prevPos = prevCursor.position();
    QPlainTextEdit::keyPressEvent(e);
    QTextCursor afterCursor = textCursor();
    int newPos = afterCursor.position();

    if (document()->revision() != prevRevision)
    {
        // A genuine edit happened through Qt's own single-cursor machinery
        // (typing, a plain Backspace, Ctrl+V, ...) - any snapshot we saved
        // for multi-cursor undo/redo no longer sits on top of the
        // document's undo stack, so forget it.
        _multiUndoHistory.clear();
        _multiRedoHistory.clear();
    }

    // Only resync (which collapses to a single cursor) if the native cursor
    // actually moved or its selection changed. A bare modifier key press
    // (e.g. pressing Alt down before an Alt+Click) also reaches this point
    // but must NOT wipe out an existing multi-cursor selection.
    if (newPos != prevPos || afterCursor.anchor() != prevCursor.anchor())
        syncFromNativeCursor();
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

    if (!_multiCursor.isMultiple())
    {
        insertPlainText(source->text());
        return;
    }

    // Multi-cursor paste: if the clipboard has exactly as many lines as
    // there are cursors, distribute one line per cursor (top-to-bottom),
    // like VS Code. Otherwise paste the whole text into every cursor.
    QStringList lines = source->text().split('\n');
    bool perCursor = (lines.size() == _multiCursor.count());

    QVector<int> ascending = cursorsOrderedByPosition();
    QVector<int> lineForCursor(_multiCursor.count());
    for (int rank = 0; rank < ascending.size(); ++rank)
        lineForCursor[ascending[rank]] = rank;

    QVector<int> order = ascending;
    std::reverse(order.begin(), order.end()); // edit highest position first

    QVector<QPair<int,int>> preState = snapshotCursors(_multiCursor);

    _multiCursor.cursors()[order.first()].beginEditBlock();
    for (int i : std::as_const(order))
    {
        QTextCursor &c = _multiCursor.cursors()[i];
        QString t = perCursor ? lines[lineForCursor[i]] : source->text();
        if (c.hasSelection())
            c.removeSelectedText();
        c.insertText(t);
    }
    _multiCursor.cursors()[order.first()].endEditBlock();
    _multiCursor.mergeOverlapping();

    syncToNativeCursor();
    recordMultiEditUndo(preState);
}

namespace
{
    // Same background handling used by QPlainTextEdit for block backgrounds.
    // Keeping the brush origin/gradient transform matters for styles that use
    // gradients and for WaveUnderline rendering.
    static void fillEditorBackground(QPainter *painter,
                                      const QRectF &rect,
                                      QBrush brush,
                                      const QRectF &gradientRect = QRectF())
    {
        painter->save();

        if (brush.style() >= Qt::LinearGradientPattern
            && brush.style() <= Qt::ConicalGradientPattern
            && !gradientRect.isNull())
        {
            QTransform transform = QTransform::fromTranslate(
                gradientRect.left(), gradientRect.top());
            transform.scale(gradientRect.width(), gradientRect.height());
            brush.setTransform(transform);

            if (brush.gradient())
                const_cast<QGradient *>(brush.gradient())->setCoordinateMode(
                    QGradient::LogicalMode);
        }
        else
        {
            painter->setBrushOrigin(rect.topLeft());
        }

        painter->fillRect(rect, brush);
        painter->restore();
    }
}

void CodeEditor::paintEvent(QPaintEvent *event)
{
    if (!_multiCursor.isMultiple())
    {
        // Keep the complete native QPlainTextEdit implementation for the
        // ordinary single-cursor case.
        QPlainTextEdit::paintEvent(event);
        return;
    }

    // Do NOT call QPlainTextEdit::paintEvent() here. Its implementation uses
    // getPaintContext().cursorPosition and eventually calls
    // QTextLayout::drawCursor() for the native cursor. On Windows/Fusion that
    // cursor can survive cursorWidth(0), and it has its own blink/geometry.
    //
    // Instead, this is the relevant QPlainTextEdit paint path with exactly
    // the same document/layout/selection machinery, but with the native
    // cursor disabled. Our synchronized cursors are drawn below using the
    // very same QTextLayout::drawCursor() implementation.
    QPainter painter(viewport());
    Q_ASSERT(qobject_cast<QPlainTextDocumentLayout *>(document()->documentLayout()));

    QPointF offset = contentOffset();
    QRect er = event->rect();
    QRect viewportRect = viewport()->rect();

    QTextBlock block = firstVisibleBlock();
    const qreal maximumWidth = document()->documentLayout()->documentSize().width();

    painter.setBrushOrigin(offset);

    // Keep right margin clean from full-width selections.
    const int maxX = int(offset.x()
                        + qMax(qreal(viewportRect.width()), maximumWidth)
                        - document()->documentMargin()
                        + cursorWidth());
    er.setRight(qMin(er.right(), maxX));
    painter.setClipRect(er);

    if (document()->isEmpty() && !placeholderText().isEmpty())
    {
        const QColor col = palette().placeholderText().color();
        painter.setPen(col);
        painter.setClipRect(event->rect());

        const int margin = int(document()->documentMargin());
        QRectF textRect = viewportRect.adjusted(margin, margin, 0, 0);
        painter.drawText(textRect,
                         Qt::AlignTop | Qt::TextWordWrap,
                         placeholderText());
    }

    QAbstractTextDocumentLayout::PaintContext context = getPaintContext();

    // This is the crucial part: let Qt draw the text and all ExtraSelections,
    // but explicitly suppress its native caret. In Qt's own paintEvent(),
    // cursorPosition is what causes QTextLayout::drawCursor() to be called.
    context.cursorPosition = -1;

    painter.setPen(context.palette.text().color());

    const bool overwrite = _overwriteMode;

    while (block.isValid())
    {
        QRectF r = blockBoundingRect(block).translated(offset);
        QTextLayout *layout = block.layout();

        if (!block.isVisible())
        {
            offset.ry() += r.height();
            block = block.next();
            continue;
        }

        if (r.bottom() >= er.top() && r.top() <= er.bottom())
        {
            QTextBlockFormat blockFormat = block.blockFormat();
            QBrush bg = blockFormat.background();

            if (bg != Qt::NoBrush)
            {
                QRectF contentsRect = r;
                contentsRect.setWidth(qMax(r.width(), maximumWidth));
                fillEditorBackground(&painter, contentsRect, bg, contentsRect);
            }

            QList<QTextLayout::FormatRange> selections;
            const int blpos = block.position();
            const int bllen = block.length();

            // In overwrite mode, Qt paints the character under the caret
            // using the caret color and redraws the character itself using
            // the editor background color.  Add these masks to the
            // QTextLayout::FormatRange list for THIS block.  They must not
            // be appended to context.selections: that list contains
            // QAbstractTextDocumentLayout::Selection objects, not
            // QTextLayout::FormatRange objects.
            if (overwrite)
            {
                const QPalette &pal = context.palette;

                for (const QTextCursor &cur : std::as_const(_multiCursor.cursors()))
                {
                    if (cur.block() != block)
                        continue;

                    QTextCursor charCur(cur);
                    charCur.clearSelection();

                    const int cpos = charCur.position() - blpos;
                    if (cpos < 0 || cpos >= bllen - 1)
                        continue; // EOL: draw a normal thin caret below.

                    QTextLayout::FormatRange mask;
                    mask.start = cpos;
                    mask.length = 1;
                    mask.format.setForeground(pal.base());
                    mask.format.setBackground(pal.text());
                    selections.append(mask);
                }
            }

            for (const QAbstractTextDocumentLayout::Selection &range : std::as_const(context.selections))
            {
                const int selStart = range.cursor.selectionStart() - blpos;
                const int selEnd = range.cursor.selectionEnd() - blpos;

                if (selStart < bllen && selEnd > 0 && selEnd > selStart)
                {
                    QTextLayout::FormatRange o;
                    o.start = selStart;
                    o.length = selEnd - selStart;
                    o.format = range.format;
                    selections.append(o);
                }
                else if (!range.cursor.hasSelection()
                         && range.format.hasProperty(QTextFormat::FullWidthSelection)
                         && block.contains(range.cursor.position()))
                {
                    // Same full-width selection handling as QPlainTextEdit.
                    QTextLayout::FormatRange o;
                    QTextLine line = layout->lineForTextPosition(
                        range.cursor.position() - blpos);
                    o.start = line.textStart();
                    o.length = line.textLength();

                    if (o.start + o.length == bllen - 1)
                        ++o.length; // include newline

                    o.format = range.format;
                    selections.append(o);
                }
            }

            // QTextLayout::draw() uses the exact same fractional text
            // geometry as Qt's native caret. No QRect/toRect conversion is
            // involved here.
            layout->draw(&painter, offset, selections, er);

            if (_caretBlinkVisible)
            {
                painter.setPen(context.palette.text().color());

                for (const QTextCursor &cur : _multiCursor.cursors())
                {
                    if (cur.block() != block)
                        continue;

                    const int cpos = cur.position() - blpos;

                    if (cpos < 0 || cpos > bllen - 1)
                        continue;

                    if (overwrite)
                    {
                        // Qt's overwrite caret is represented by the masked
                        // character above. At EOL it falls back to a normal
                        // thin caret, so only draw the thin caret there.
                        if (cpos < bllen - 1)
                            continue;
                    }

                    // This is deliberately QTextLayout::drawCursor(), not
                    // QPainter::drawLine()/fillRect(). Qt performs all the
                    // fractional-DPI rounding and bidi-aware cursor geometry
                    // here, so our caret lands exactly where Qt's native caret
                    // would have landed.
                    layout->drawCursor(
                        &painter,
                        offset,
                        cpos,
                        qMax(1, _nativeCursorWidth));
                }
            }
        }

        offset.ry() += r.height();

        if (offset.y() > viewportRect.height())
            break;

        block = block.next();
    }

    // Match QPlainTextEdit's background handling below the last document block.
    if (backgroundVisible()
        && !block.isValid()
        && offset.y() <= er.bottom()
        && (centerOnScroll()
            || verticalScrollBar()->maximum() == verticalScrollBar()->minimum()))
    {
        painter.fillRect(
            QRect(QPoint(er.left(), int(offset.y())), er.bottomRight()),
            palette().window());
    }
}

void CodeEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && event->modifiers().testFlag(Qt::AltModifier))
    {
        _multiCursor.addCursor(cursorForPosition(event->pos()));
        syncToNativeCursor();
        event->accept();
        return;
    }

    QPlainTextEdit::mousePressEvent(event);
    syncFromNativeCursor();
}

void CodeEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons().testFlag(Qt::LeftButton) && event->modifiers().testFlag(Qt::AltModifier))
    {
        // An Alt+Click that starts a drag is meant to give the *new* cursor
        // a real selection, not just a caret. mousePressEvent skipped the
        // base class entirely for that Alt+Click (so nothing there is
        // tracking a drag), so without this override the mouse motion was
        // simply dropped: the added cursor stayed a zero-length caret,
        // hasSelection() was false, and multiCursorSelectedText() (rightly)
        // ignored it - which is why copying a multi-cursor selection made
        // this way silently lost whatever was dragged out with Alt held.
        QTextCursor &cur = _multiCursor.cursors()[_multiCursor.mainIndex()];
        QTextCursor moved = cursorForPosition(event->pos());
        cur.setPosition(moved.position(), QTextCursor::KeepAnchor);
        syncToNativeCursor();
        event->accept();
        return;
    }

    QPlainTextEdit::mouseMoveEvent(event);
}

void CodeEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->modifiers().testFlag(Qt::AltModifier))
    {
        // mousePressEvent skipped the base class entirely for an Alt+Click
        // (no drag/press state was ever registered there), so skip the
        // matching release too instead of letting Qt act on stale state.
        event->accept();
        return;
    }

    QPlainTextEdit::mouseReleaseEvent(event);
    syncFromNativeCursor();
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
        for (int i = 0; i < selectedText.size(); ++i)
        {
            const QChar c = selectedText.at(i);
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

            auto verifyPos = [&selections, wordLength, &s, &testCursor, &content, &total_hits](int &pos, int &counter)
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
            right_bracket_selections.append(matchBracket(content, c, left_bracket_selections.empty() ? 100 : 130));
        }
    }

    selections += right_bracket_selections + left_bracket_selections + baseExtraSelections();
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
    QChar c1 = selectedBracket.selectedText().at(0);
    QString brackets("([{)]}");
    int c1pos = brackets.indexOf(c1, Qt::CaseInsensitive);
    // not a bracket or within commented text / string literal / so on
    if (c1pos == -1 || isEnveloped(selectedBracket.selectionStart()))
        return selections;

    // find pair character
    QChar c2 = (c1pos < 3 ? brackets[c1pos + 3] : brackets[c1pos - 3]);

    // initial selection of current bracket
    QTextEdit::ExtraSelection selection;
    if (isDarkMode())
        selection.format.setBackground(QColor(50,110,50).lighter(darkerFactor));
    else
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
        QChar c = docContent[pos];
        if ((c != c1 && c != c2) || isEnveloped(pos))
            continue;

        depth += (c == c1 ? 1 : -1);
    }
    while (depth);

    if (depth) // pair is not matched - change color of initial character
    {
        if (isDarkMode())
            selections[0].format.setBackground(QColor(110,50,50).lighter(darkerFactor));
        else
            selections[0].format.setBackground(QColor(255,160,160).darker(darkerFactor));
    }
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

QList<QTextEdit::ExtraSelection> CodeEditor::baseExtraSelections() const
{
    return currentLineSelection() + multiCursorSelections();
}

QList<QTextEdit::ExtraSelection> CodeEditor::multiCursorSelections() const
{
    QList<QTextEdit::ExtraSelection> result;
    if (!_multiCursor.isMultiple())
        return result;

    QColor highlight = palette().highlight().color();
    QColor highlightedText = palette().highlightedText().color();

    for (int i = 0; i < _multiCursor.cursors().size(); ++i)
    {
        if (i == _multiCursor.mainIndex())
            continue; // the main cursor's selection is already painted natively
        const QTextCursor &c = _multiCursor.cursors()[i];
        if (!c.hasSelection())
            continue;
        QTextEdit::ExtraSelection s;
        s.cursor = c;
        s.format.setBackground(highlight);
        s.format.setForeground(highlightedText);
        result.append(s);
    }
    return result;
}

bool CodeEditor::isEnveloped(int pos) const
{
    QTextBlock b = this->document()->findBlock(pos);
    int posInBlock = pos - b.position();
    const auto formats = b.layout()->formats();
    for (const QTextLayout::FormatRange &r: formats)
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

CodeBlockProperties::CodeBlockProperties(CodeEditor *editor) :
    QTextBlockUserData(), _editor(editor)
{
    _bookmarks.append(this);
    _lastUsedBookmarkPos = _bookmarks.size() - 1;
}

CodeBlockProperties::CodeBlockProperties(CodeEditor *editor, CodeBlockProperties *toReplace) :
    QTextBlockUserData(), _editor(editor)
{
    int bookmarkPos = _bookmarks.indexOf(toReplace);
    _bookmarks.replace(bookmarkPos, this);
    // previously freed memory could be reused by new item
    _deletedBookmarks.remove(this);
}

CodeBlockProperties::~CodeBlockProperties()
{
    // _bookmarks' item is dead and must be handled by code suspended this stuff
    if (_suspendBookmarks)
    {
        _deletedBookmarks.insert(this);
        return;
    }
    Bookmarks::remove(this);
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

void Bookmarks::remove(CodeBlockProperties *item)
{
    _lastUsedBookmarkPos = _bookmarks.indexOf(item);
    _bookmarks.removeOne(item);
    if (!_lastUsedBookmarkPos)
        _lastUsedBookmarkPos = _bookmarks.size() - 0.5f;
    else
        _lastUsedBookmarkPos -= 0.5;
}

void Bookmarks::suspend()
{
    _suspendBookmarks = true;
}

void Bookmarks::resume()
{
    _suspendBookmarks = false;
#if QT_VERSION < QT_VERSION_CHECK(6, 6, 0)
    for (CodeBlockProperties* bm: qAsConst(_deletedBookmarks))
#else
    for (CodeBlockProperties* bm: std::as_const(_deletedBookmarks))
#endif
        Bookmarks::remove(bm);
    _deletedBookmarks.clear();
}
