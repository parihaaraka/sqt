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
class QMenu;
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

    /// Marks \a range as "the place you were sent to" - a file search hit, in
    /// practice. Deliberately not the text cursor's selection: Qt paints that
    /// with the palette's Inactive group whenever the widget has no focus, and
    /// browsing the results keeps the focus in the tree, so on a dark Windows
    /// theme the selection is invisible exactly when it matters. This is our own
    /// ExtraSelection with explicit colours, so it looks the same focused or not.
    /// \a color is the hue to paint it with (the results tree's match colour, so
    /// that the two agree); an invalid one falls back to the palette.
    void setMatchHighlight(const QTextCursor &range, const QColor &color = QColor());
    void clearMatchHighlight();

    /// The lines the "copy the location of this code" command refers to,
    /// 1-based and \a first <= \a second: the caret's own line with nothing
    /// selected, otherwise from the first line any selection starts on to the
    /// last line any selection ends on (every cursor counts - see
    /// hasSelectedText - so a multi-cursor selection is reported as the whole
    /// span it covers).
    ///
    /// A selection dragged onto the very beginning of a line stops *before*
    /// that line and does not include it: naming it would point at a line the
    /// user never selected.
    QPair<int, int> selectedLineSpan() const;

protected:
    void resizeEvent(QResizeEvent *event) override;
    virtual bool eventFilter(QObject *object, QEvent *event) override;
    virtual void keyPressEvent(QKeyEvent *e) override;
    void insertFromMimeData(const QMimeData *source) override;
    QMimeData *createMimeDataFromSelection() const override;
    void paintEvent(QPaintEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
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
    /// Whether marking the occurrences of \a selectedText still makes sense:
    /// with several cursors it does only while they all hold that same word.
    bool multiCursorSharesSelection(const QString &selectedText) const;
    QList<QTextEdit::ExtraSelection> matchHighlightSelections() const; // the "sent here" mark, if any
    QList<QTextEdit::ExtraSelection> baseExtraSelections() const; // match + current line + multi-cursor, used in 3 places

    /// The one place the widget's extra selections are installed: the marks the
    /// hl timer computed (word occurrences, bracket pair) plus the ones that
    /// follow the current state (match mark, current line, extra cursors).
    ///
    /// Every caller goes through this rather than composing a list of its own and
    /// appending to extraSelections(): reading the current list back and adding to
    /// it accumulated duplicates of the match mark on every cursor move, which is
    /// what made the mark pulse (each copy is translucent, so two of them are
    /// darker than one) until the timer replaced the list wholesale.
    void applyExtraSelections();

    /// What onHlTimerTimeout() worked out from the text: the occurrences of the
    /// selected word and the matching bracket pair. Kept so that a cursor move
    /// can reinstall them without recomputing - and so that nothing has to be
    /// read back out of the widget.
    QList<QTextEdit::ExtraSelection> _computedSelections;
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

    /// Brings _multiCursor back in step with the native cursor when there is only
    /// one of them. Called at the top of handleKeyPress(), so every command below
    /// acts on what is actually on screen.
    ///
    /// _multiCursor is refreshed by this widget's own handlers alone, so a
    /// selection or a caret move made through the QPlainTextEdit API - setTextCursor(),
    /// find(), the Find/Replace panel, "select statement at the caret", a jump to a
    /// search hit - never reaches it. The copy it keeps is a live QTextCursor, so
    /// the document carries it along to wherever the text it pointed at ended up
    /// (typically the end of a freshly filled editor), and it holds no selection.
    /// Commands that read the set then act somewhere else entirely: Shift+Delete
    /// concluded "nothing is selected" and cut that stale cursor's line instead of
    /// the selection the user was looking at. Intermittently, because it depends on
    /// how the selection happened to be made.
    void refreshSingleCursorState();
    void selectNextOccurrence();               // Ctrl+D
    void addCursorOnAdjacentLine(bool below);   // Ctrl+Alt+Up / Ctrl+Alt+Down
    QString multiCursorSelectedText() const;    // every selection, joined by '\n' in document order
    const QVector<int> cursorsOrderedByPosition() const; // indices into _multiCursor.cursors(), ascending by selectionStart()
    bool copySelectionToClipboard() const;      // whatever createMimeDataFromSelection() yields -> clipboard
    static QTextCursor::MoveOperation moveOperationForKey(int key, bool ctrl);

    /// Every key the editor treats specially, dispatched by key code. True
    /// when the event is consumed, false to let QPlainTextEdit have it.
    bool handleKeyPress(QKeyEvent *keyEvent);

    /// An arrow/Home/End/PageUp/PageDown press applied to every cursor.
    /// False with a single cursor: Qt's own navigation is better left alone.
    bool moveAllCursors(int key, bool ctrl, bool shift);


    // Ctrl+Left/Right word jump lives in wordnav.h (WordNav::nextBoundary /
    // previousBoundary) - free functions over a QTextDocument, so the rules
    // can be unit-tested without an editor widget (tests/tst_wordnav.cpp).

    // Shift+Delete / Ctrl+X with nothing selected: cut whole lines. Returns
    // false when there is nothing to cut (empty document), leaving the
    // clipboard untouched.
    bool cutCurrentLines();

    // The caret's own line, or every line any selection touches, as one
    // range covering them whole (leading '\n' included where there is one) -
    // what cutCurrentLines() removes and puts on the clipboard.
    QTextCursor lineRangeForCut(const QTextCursor &c) const;

    // Is there any text to act on - the native selection, or that of any of
    // the several cursors? Also what decides whether the case items of the
    // context menu are enabled.
    bool hasSelectedText() const;

    // Ctrl+U / Ctrl+Shift+U and the matching context menu items: fold every
    // selection to \a upper case (or to lower), keeping it selected.
    void changeSelectedTextCase(bool upper);


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

    // Raised by insertFromMimeData() when it distributed a paste across
    // several cursors itself. keyPressEvent() checks it right after handing
    // the key to QPlainTextEdit (Ctrl+V / Shift+Insert arrive that way): the
    // edit did not go through Qt's single native cursor, so the single-cursor
    // tail there must be skipped entirely - syncFromNativeCursor() would
    // collapse the whole set back to one cursor (making a second paste
    // impossible without recreating every cursor by hand), and the undo
    // history reset would drop the snapshot the paste has just recorded.
    bool _multiPasteHandled = false;

    QCompleter *_completer = nullptr;
    MultiTextCursor _multiCursor;
    /// How the *press* of the gesture in progress was classified, latched so
    /// that the move and release handlers cannot disagree with it when Alt is
    /// pressed or released mid-drag. See CodeEditor::mousePressEvent().
    bool _altGesture = false;
    int _nativeCursorWidth = 1;

    // The "sent here" mark (see setMatchHighlight). A cursor with a selection
    // when active, its colour alongside; kept in baseExtraSelections() so it
    // survives every rehighlight, which rebuilds the selection list from scratch.
    QTextCursor _matchHighlight;
    QColor _matchHighlightColor;

signals:
    void completerRequest();
    void scriptObjectRequest();
    /// Ctrl+Return: run the current selection, or (with none) the statement
    /// under the caret - see MainWindow::executeQuery().
    void executeStatementRequest();
    /// Ctrl+Shift+A: select the statement under the caret without running
    /// it - a dry-run preview of what Ctrl+Return would send with no
    /// selection.
    void selectStatementRequest();
    /// Ctrl+Shift+C: put the place being read - file and line(s) - on the
    /// clipboard, to be pasted into an ai agent's prompt. The editor does not
    /// do it itself: which file its text belongs to (if any) is the owner's
    /// knowledge, see QueryWidget::codeLocation().
    void copyCodeLocationRequest();
    /// The standard context menu, for the owner to append its own commands to
    /// before it is shown. Only the owner knows whether they apply (a
    /// connection, a dbms with a statement separator), so the editor builds
    /// the menu and hands it over rather than deciding anything itself.
    void contextMenuRequest(QMenu *menu);
};

#endif // CODEEDITOR_H
