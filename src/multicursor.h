#ifndef MULTICURSOR_H
#define MULTICURSOR_H

#include <QTextCursor>
#include <QTextDocument>
#include <QVector>
#include <algorithm>

// Manages a set of simultaneously active text cursors ("multi-cursor" editing).
//
// Every cursor kept here is a *live* QTextCursor, registered with its
// QTextDocument. Qt automatically keeps a live cursor's position correct
// whenever text is inserted/removed anywhere in the document - including
// edits made through *other* cursors in this same set. That guarantee is
// what makes it safe to simply loop over the set and edit through each
// cursor in turn: as long as edits are applied from the highest document
// position down to the lowest, no cursor's position calculation is ever
// invalidated by an edit made earlier in the same loop.
class MultiTextCursor
{
public:
    bool isMultiple() const { return _cursors.size() > 1; }
    bool isEmpty() const { return _cursors.isEmpty(); }
    int count() const { return _cursors.size(); }
    int mainIndex() const { return _mainIndex; }

    QTextCursor mainCursor() const
    {
        return (_mainIndex >= 0 && _mainIndex < _cursors.size()) ? _cursors[_mainIndex] : QTextCursor();
    }

    const QVector<QTextCursor>& cursors() const { return _cursors; }
    QVector<QTextCursor>& cursors() { return _cursors; }

    // Resets the whole set to a single cursor - i.e. plain, single-cursor
    // editing. Call this whenever the widget's native cursor changes in a
    // way we didn't drive ourselves (plain mouse click, Escape, etc).
    void setCursors(const QTextCursor &cursor)
    {
        _cursors.clear();
        _cursors.append(cursor);
        _mainIndex = 0;
    }

    // Adds a new cursor and makes it the main one. If a cursor already
    // exists at exactly the same position with no selection, it is not
    // duplicated (matches the common Alt+Click-twice-on-the-same-spot
    // behaviour of VS Code / Qt Creator).
    void addCursor(const QTextCursor &cursor)
    {
        if (!cursor.hasSelection())
        {
            for (const QTextCursor &c : std::as_const(_cursors))
            {
                if (!c.hasSelection() && c.position() == cursor.position())
                {
                    _mainIndex = _cursors.indexOf(c);
                    return;
                }
            }
        }
        _cursors.append(cursor);
        _mainIndex = _cursors.size() - 1;
    }

    // Drops overlapping / duplicate cursors - needed after every edit, and
    // after operations like "select next occurrence" that might land a new
    // cursor's selection on top of an existing one.
    void mergeOverlapping()
    {
        if (_cursors.size() < 2)
            return;

        QTextCursor mainBefore = mainCursor();

        QVector<QTextCursor> sorted = _cursors;
        std::sort(sorted.begin(), sorted.end(), [](const QTextCursor &a, const QTextCursor &b) {
            return a.selectionStart() < b.selectionStart();
        });

        QVector<QTextCursor> merged;
        for (const QTextCursor &c : std::as_const(sorted))
        {
            if (!merged.isEmpty())
            {
                QTextCursor &last = merged.last();
                bool touching = (c.selectionStart() <= last.selectionEnd());
                if (touching)
                {
                    int start = std::min(last.selectionStart(), c.selectionStart());
                    int end = std::max(last.selectionEnd(), c.selectionEnd());
                    QTextCursor combined(c.document());
                    combined.setPosition(start);
                    combined.setPosition(end, QTextCursor::KeepAnchor);
                    last = combined;
                    continue;
                }
            }
            merged.append(c);
        }

        _cursors = merged;

        // Find which merged cursor now covers where the previous main
        // cursor used to be, so "main" doesn't jump around after a merge.
        _mainIndex = 0;
        for (int i = 0; i < _cursors.size(); ++i)
        {
            if (_cursors[i].selectionStart() <= mainBefore.position() &&
                mainBefore.position() <= _cursors[i].selectionEnd())
            {
                _mainIndex = i;
                break;
            }
        }
    }

    // Applies an edit through every cursor, highest document position
    // first, wrapped in a single undo step.
    template <typename Fn>
    void editBlock(Fn fn)
    {
        if (_cursors.isEmpty())
            return;

        QVector<int> order(_cursors.size());
        for (int i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [this](int a, int b) {
            return _cursors[a].selectionStart() > _cursors[b].selectionStart();
        });

        _cursors[order.first()].beginEditBlock();
        for (int i : std::as_const(order))
            fn(_cursors[i]);
        _cursors[order.first()].endEditBlock();

        mergeOverlapping();
    }

    // Applies a pure cursor-movement operation to every cursor. Order does
    // not matter since nothing is edited.
    template <typename Fn>
    void forEachCursor(Fn fn)
    {
        for (QTextCursor &c : _cursors)
            fn(c);
        mergeOverlapping();
    }

private:
    QVector<QTextCursor> _cursors;
    int _mainIndex = -1;
};

#endif // MULTICURSOR_H
