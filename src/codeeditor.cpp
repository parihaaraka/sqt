#include "codeeditor.h"
#include <QPainter>
#include <QTextBlock>

#define RIGHT_MARGIN 2
#define ICON_PLACE_WIDTH 10

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
    painter.fillRect(event->rect(), palette().window());

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top = (int) blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int) blockBoundingRect(block).height();

    QFont curFont(font());
    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            QString number = QString::number(blockNumber + 1);
            if (curBlock == blockNumber)
            {
                QFont curLineFont = curFont;
                curLineFont.setBold(true);
                painter.setFont(curLineFont);
            }
            else
                painter.setFont(curFont);
            painter.setPen(curBlock == blockNumber ? Qt::darkBlue : Qt::darkGray);
            painter.drawText(0, top, _leftSideBar->width() - RIGHT_MARGIN, fontMetrics().height(),
                             Qt::AlignRight, number);
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
