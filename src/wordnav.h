#ifndef WORDNAV_H
#define WORDNAV_H

class QTextDocument;

/// Word-wise caret movement (Ctrl+Left / Ctrl+Right), VS Code style.
///
/// Two rules, neither of which QTextCursor::NextWord/PreviousWord gives us -
/// hence walking the document by hand:
///
/// 1. A run of whitespace is always swallowed on the way but is never itself a
///    stopping point: the caret lands right where a word/punctuation run ends
///    (nextBoundary) or begins (previousBoundary), so Ctrl+Shift+Right
///    selecting a word doesn't pull in the space after it. One exception: a
///    single separator glued directly onto a word with no space between (the
///    '.' in "qwe.rty") isn't its own stop - it merges into that word,
///    matching VS Code.
///
/// 2. A jump stays inside the caret's own line. The line's own edge is a stop,
///    and only a jump made *from* that edge crosses into the neighbouring
///    line. So Ctrl+Left from the first non-blank character of an indented
///    line lands at column 0 (before the indentation), and only the next press
///    moves up - instead of skipping the indentation and the line break in one
///    go and landing in the middle of the line above. An empty line is a stop
///    of its own, same as in VS Code.
namespace WordNav
{
    /// Where Ctrl+Right should take a caret standing at \a pos.
    int nextBoundary(const QTextDocument *doc, int pos);

    /// Where Ctrl+Left should take a caret standing at \a pos.
    int previousBoundary(const QTextDocument *doc, int pos);
}

#endif // WORDNAV_H
