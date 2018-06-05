#include <QtGlobal>
#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>
#include <QMimeData>
#include <QRegularExpression>
#include <QTimer>
#include "settings.h"

#define RIGHT_MARGIN 2
#define ICON_PLACE_WIDTH 13

QList<CodeBlockProperties*> _bookmarks;
float _lastUsedBookmarkPos = 0;

class LeftSideBar : public QWidget
{
public:
    LeftSideBar(CodeEditor *editor) : QWidget(editor) {
        _codeEditor = editor;
    }

    QSize sizeHint() const override {
        return QSize(_codeEditor->leftSideBarWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override {
        _codeEditor->leftSideBarPaintEvent(event);
    }

private:
    CodeEditor *_codeEditor;
};

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
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

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
        bottom = top + (int) blockBoundingRect(block).height();
        ++blockNumber;
    }
}

int CodeEditor::leftSideBarWidth()
{
    return ICON_PLACE_WIDTH + RIGHT_MARGIN +
            fontMetrics().width(QString::number(blockCount()));
}

void CodeEditor::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    _leftSideBar->setGeometry(QRect(cr.left(), cr.top(), leftSideBarWidth(), cr.height()));
}

bool CodeEditor::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::FontChange)
    {
        _leftSideBar->setFont(font());
        updateLeftSideBarWidth();
    }
    return false;
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
        int curPos = curCursor.selectionStart();
        QTextCursor testCursor = textCursor();
        testCursor.setPosition(curPos);
        testCursor.select(QTextCursor::WordUnderCursor);
        if (selectedText == testCursor.selectedText())
        {
#if QT_VERSION >= 0x050900
            content = document()->toRawText();
#else
            content = document()->toPlainText();
#endif
            int pos = 0;
            QColor selectionColor(Qt::yellow);
            selectionColor.setAlphaF(0.7);
            while (true)
            {
                pos = content.indexOf(selectedText, pos);
                if (pos < 0)
                    break;
                if (curPos != pos)
                {
                    QTextCursor testCursor = textCursor();
                    testCursor.setPosition(pos);
                    testCursor.select(QTextCursor::WordUnderCursor);
                    if (selectedText == testCursor.selectedText())
                    {
                        QTextEdit::ExtraSelection s;
                        s.format.setBackground(selectionColor);
                        s.cursor = testCursor;
                        selections.append(s);
                    }
                }
                pos += selectedText.length();
            }
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

QList<QTextEdit::ExtraSelection> CodeEditor::matchBracket(QString &docContent, const QTextCursor &selectedBracket, int darkerFactor)
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
#if QT_VERSION >= 0x050900
        docContent = document()->toRawText();
#else
        docContent = document()->toPlainText();
#endif

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

QList<QTextEdit::ExtraSelection> CodeEditor::currentLineSelection()
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

bool CodeEditor::isEnveloped(int pos)
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

CodeBlockProperties::CodeBlockProperties(QueryWidget *w) : QTextBlockUserData(), _w(w)
{
    _bookmarks.append(this);
    _lastUsedBookmarkPos = _bookmarks.size() - 1;
}

CodeBlockProperties::~CodeBlockProperties()
{
    _lastUsedBookmarkPos = _bookmarks.indexOf(this);
    _bookmarks.removeOne(this);
    if (!_lastUsedBookmarkPos)
        _lastUsedBookmarkPos = _bookmarks.size() - 0.5;
    else
        _lastUsedBookmarkPos -= 0.5;
}

QueryWidget *CodeBlockProperties::queryWidget() const
{
    return _w;
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
    return _bookmarks[_lastUsedBookmarkPos];
}

CodeBlockProperties *Bookmarks::previous()
{
    if (_bookmarks.isEmpty())
        return nullptr;

    if (_bookmarks.size() == 1)
        return _bookmarks[0];

    // use current position (if not deleted) or jump to next existing one before search previous
    _lastUsedBookmarkPos = static_cast<int>(_lastUsedBookmarkPos + 0.5);

    _lastUsedBookmarkPos = (_lastUsedBookmarkPos > 0 ?
                                _lastUsedBookmarkPos - 1 : _bookmarks.size() - 1);
    return _bookmarks[_lastUsedBookmarkPos];
}

CodeBlockProperties *Bookmarks::last()
{
    if (_bookmarks.isEmpty())
        return nullptr;

    if (_lastUsedBookmarkPos != static_cast<int>(_lastUsedBookmarkPos))
        return _bookmarks[_bookmarks.size() - 1];

    return _bookmarks[_lastUsedBookmarkPos];
}
