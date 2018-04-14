#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>

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
    connect(this, &CodeEditor::blockCountChanged, this, &CodeEditor::updateLeftSideBarWidth);
    connect(this, &CodeEditor::updateRequest, this, &CodeEditor::updateLeftSideBar);
    connect(this, &CodeEditor::cursorPositionChanged, _leftSideBar, static_cast<void(QWidget::*)(void)>(&QWidget::update));
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

            // draw line number
            QColor penColor = palette().windowText().color();
            penColor.setAlphaF(curBlock == blockNumber ? 0.7 : 0.4);
            painter.setPen(penColor);
            painter.drawText(0,
                             top,
                             _leftSideBar->width() - RIGHT_MARGIN,
                             fontMetrics().height(),
                             Qt::AlignRight,
                             QString::number(blockNumber + 1));
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
