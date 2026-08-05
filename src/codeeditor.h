#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <QPlainTextEdit>
#include <QTextBlockUserData>
#include <QPair>
#include <functional>
#include "multicursor.h"

class CodeBlockProperties;
class QCompleter;
class QTimer;
class CodeEditor;

namespace Bookmarks
{
    CodeBlockProperties* next();
    CodeBlockProperties* previous();
    CodeBlockProperties* last();
    void remove(CodeBlockProperties* item);
    void suspend();
    void resume();
}

class CodeBlockProperties : public QTextBlockUserData
{
public:
    CodeBlockProperties(CodeEditor *editor, CodeBlockProperties *toReplace);
    CodeBlockProperties(CodeEditor *editor);
    ~CodeBlockProperties();
    CodeEditor* editor() const;
private:
    // bool _isBookmarked = true;
    CodeEditor *_editor;
};

class CodeEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    CodeEditor(QWidget *parent = nullptr);
    void leftSideBarPaintEvent(QPaintEvent *event);
    int leftSideBarWidth() const;
    QString text() const;
    void setCompleter(QCompleter *completer);

    // Multi-cursor
    bool hasMultipleCursors() const { return _multiCursor.isMultiple(); }
    void collapseToSingleCursor();

protected:
    void resizeEvent(QResizeEvent *event) override;
    virtual bool eventFilter(QObject *object, QEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *e) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private slots:
    void updateLeftSideBarWidth();
    void updateLeftSideBar(const QRect &rect, int dy);
    void onHlTimerTimeout();
    void insertCompletion(const QString &completion);
    void onCaretBlink();

private:
    QList<QTextEdit::ExtraSelection> matchBracket(QString &docContent, const QTextCursor &selectedBracket, int darkerFactor = 100) const;
    QList<QTextEdit::ExtraSelection> currentLineSelection() const;
    QList<QTextEdit::ExtraSelection> multiCursorSelections() const;
    QList<QTextEdit::ExtraSelection> baseExtraSelections() const; // currentLineSelection() + multiCursorSelections(), used in 3 places
    bool isEnveloped(int pos) const;
    int indentSize() const; // small wrapper so the setting key/default lives in one place

    // ---- multi-cursor plumbing ----
    void syncFromNativeCursor();   // native textCursor() -> _multiCursor (call after anything that moved the native cursor without going through us)
    void syncToNativeCursor();     // _multiCursor's main cursor -> native textCursor() (call after anything that changed _multiCursor ourselves)
    void selectNextOccurrence();               // Ctrl+D
    void addCursorOnAdjacentLine(bool below);   // Ctrl+Alt+Up / Ctrl+Alt+Down
    QString multiCursorSelectedText() const;    // every selection, joined by '\n' in document order
    QVector<int> cursorsOrderedByPosition() const; // indices into _multiCursor.cursors(), ascending by selectionStart()
    bool copySelectionToClipboard() const;      // whatever createMimeDataFromSelection() yields -> clipboard
    static bool isNavigationKey(int key);
    static QTextCursor::MoveOperation moveOperationForKey(int key, bool ctrl);

    // ---- per-cursor editing primitives ----
    // Identical logic whether there is one active cursor or many: with a
    // single cursor these are called once directly, with several cursors
    // they are called once per cursor inside a MultiTextCursor::editBlock.
    bool applySmartBackspace(QTextCursor &c);   // returns true if it did a custom (indent-aware) delete
    void applyReturnWithIndent(QTextCursor &c);
    void applyHome(QTextCursor &c, bool keepAnchor);
    void applyMultiLineIndent(QTextCursor &c, bool forward);
    void applySingleLineTab(QTextCursor &c);
    void applySingleLineBacktab(QTextCursor &c);
    static int visualColumnAt(QTextDocument *doc, int blockStart, int pos, int indentSize);

    // Common tail shared by every single-cursor edit handler below (Backspace,
    // Return, Tab/Backtab, upper/lowercase toggle): push the edited cursor
    // back as the real one, drop back to single-cursor bookkeeping, and
    // invalidate any pending multi-cursor undo/redo snapshot (this edit
    // didn't go through performMultiEdit(), so those snapshots no longer
    // apply).
    void applySingleCursorEdit(QTextCursor &c);

    // ---- multi-cursor-aware undo/redo ----
    // Qt's document undo/redo is a single stack shared by everything; it
    // has no idea several cursors were involved in an edit. So we keep our
    // own one-step-deep memory of "the cursor set right before/after the
    // last multi-cursor edit" and intercept Ctrl+Z/Ctrl+Y ourselves to
    // restore it in lockstep with the document undo/redo.
    void performMultiEdit(std::function<void(QTextCursor&)> fn);
    static QVector<QPair<int,int>> snapshotCursors(const MultiTextCursor &mc); // {anchor, position} per cursor
    void restoreCursorSnapshot(const QVector<QPair<int,int>> &snapshot);

    QVector<QPair<int,int>> _preMultiEditCursorState;
    QVector<QPair<int,int>> _postMultiEditCursorState;
    bool _multiUndoAvailable = false;
    bool _multiRedoAvailable = false;

    QWidget *_leftSideBar;
    QTimer *_hlTimer;
    QTimer *_caretBlinkTimer;
    bool _caretBlinkVisible = true;

    // Logical overwrite (Ins) state. Mirrors QPlainTextEdit's own
    // overwriteMode() *except* while several cursors are active: Qt's
    // native overwrite caret ignores setCursorWidth(0) and keeps drawing
    // itself (on its own blink cycle) regardless, so with multiple cursors
    // we turn the native flag off and do the block-caret drawing and the
    // "eat next character" typing logic ourselves, driven by this flag.
    bool _overwriteMode = false;
    QCompleter *_completer = nullptr;
    MultiTextCursor _multiCursor;
    int _nativeCursorWidth = 1;

signals:
    void completerRequest();
    void scriptObjectRequest();
};

#endif // CODEEDITOR_H
