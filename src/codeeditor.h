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
    ~CodeEditor() override;
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

    // Force the caret(s) fully visible and restart the blink countdown from
    // zero. Called on every key press while multiple cursors are active, so
    // the caret stays solid during movement/typing/deletion (incl. held-key
    // autorepeat) and only starts blinking once the keyboard goes idle -
    // matching VS Code, terminals, and Qt's own single-cursor behavior.
    void resetCaretBlink();

    // ---- multi-cursor plumbing ----
    void syncFromNativeCursor();   // native textCursor() -> _multiCursor (call after anything that moved the native cursor without going through us)
    void syncToNativeCursor();     // _multiCursor's main cursor -> native textCursor() (call after anything that changed _multiCursor ourselves)
    void selectNextOccurrence();               // Ctrl+D
    void addCursorOnAdjacentLine(bool below);   // Ctrl+Alt+Up / Ctrl+Alt+Down
    QString multiCursorSelectedText() const;    // every selection, joined by '\n' in document order
    const QVector<int> cursorsOrderedByPosition() const; // indices into _multiCursor.cursors(), ascending by selectionStart()
    bool copySelectionToClipboard() const;      // whatever createMimeDataFromSelection() yields -> clipboard
    static bool isNavigationKey(int key);
    static QTextCursor::MoveOperation moveOperationForKey(int key, bool ctrl);

    // Ctrl+Left/Right word jump, VS Code style: a run of whitespace is
    // always swallowed on the way but never itself a stopping point - the
    // cursor lands right where a word/punctuation run ends (nextWordBoundary)
    // or begins (previousWordBoundary), so Ctrl+Shift+Right selecting a word
    // doesn't pull in the space after it. One exception: a single separator
    // glued directly onto a word with no space between (the '.' in
    // "qwe.rty") isn't its own stop - it merges into that word, matching VS
    // Code. QTextCursor::NextWord/PreviousWord give us neither of this, so
    // this walks the document by hand instead.
    static int nextWordBoundary(QTextDocument *doc, int pos);
    static int previousWordBoundary(QTextDocument *doc, int pos);

    // ---- per-cursor editing primitives ----
    // Identical logic whether there is one active cursor or many: with a
    // single cursor these are called once directly, with several cursors
    // they are called once per cursor inside a MultiTextCursor::editBlock.
    bool applySmartBackspace(QTextCursor &c);   // returns true if it did a custom (indent-aware) delete
    void applyReturnWithIndent(QTextCursor &c);
    void applyHome(QTextCursor &c, bool keepAnchor);
    void applyMultiLineIndent(QTextCursor &c, bool forward);
    void applySingleLineTab(QTextCursor &c);
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
    // has no idea several cursors were involved in an edit. Keep a parallel
    // stack of cursor snapshots so consecutive Ctrl+Z/Ctrl+Y operations can
    // restore the whole multi-cursor set, not just the most recent edit.
    void performMultiEdit(std::function<void(QTextCursor&)> fn);
    void recordMultiEditUndo(const QVector<QPair<int,int>> &preState);
    static QVector<QPair<int,int>> snapshotCursors(const MultiTextCursor &mc); // {anchor, position} per cursor
    void restoreCursorSnapshot(const QVector<QPair<int,int>> &snapshot);

    struct MultiEditCursorHistory
    {
        QVector<QPair<int,int>> pre;
        QVector<QPair<int,int>> post;
    };

    QVector<MultiEditCursorHistory> _multiUndoHistory;
    QVector<MultiEditCursorHistory> _multiRedoHistory;

    QWidget *_leftSideBar;
    QTimer *_hlTimer;
    QTimer *_caretBlinkTimer;
    bool _caretBlinkVisible = true;

    // Logical overwrite (Ins) state. Mirrors QPlainTextEdit's own
    // overwriteMode() except while several cursors are active: the native
    // cursor is completely suppressed by our paintEvent(), and overwrite
    // caret rendering is handled for every active cursor using the same
    // QTextLayout geometry as Qt's own renderer.
    bool _overwriteMode = false;
    QCompleter *_completer = nullptr;
    MultiTextCursor _multiCursor;
    int _nativeCursorWidth = 1;

signals:
    void completerRequest();
    void scriptObjectRequest();
};

#endif // CODEEDITOR_H
