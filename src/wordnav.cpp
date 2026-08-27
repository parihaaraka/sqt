#include "wordnav.h"
#include <QTextDocument>

namespace
{
    // A line break is deliberately *not* whitespace here. QChar::isSpace()
    // covers QChar::ParagraphSeparator (what QTextDocument stores between
    // blocks), and lumping the two together is what used to make Ctrl+Left
    // from the first non-blank character of an indented line skip the
    // indentation *and* the break in one go, landing in the middle of the
    // line above. Keeping it a class of its own turns the line's edge into
    // a stop of its own - see the rules in wordnav.h.
    enum class CharClass { LineBreak, Space, Word, Other };

    CharClass classify(QChar ch)
    {
        // A bare '\r' or '\n' should not normally reach us (QTextDocument
        // stores neither), but text set programmatically can carry one, and
        // it means "line break" just the same.
        if (ch == QChar::ParagraphSeparator || ch == QChar::LineSeparator ||
            ch == QLatin1Char('\n') || ch == QLatin1Char('\r'))
            return CharClass::LineBreak;
        if (ch.isSpace())
            return CharClass::Space;
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_'))
            return CharClass::Word;
        return CharClass::Other;
    }
}

int WordNav::nextBoundary(const QTextDocument *doc, int pos)
{
    // characterCount() counts one extra (implicit, non-printable) trailing
    // character; the last real, addressable position is one before it.
    const int end = doc->characterCount() - 1;
    if (pos < 0)
        pos = 0;
    if (pos >= end)
        return end;

    // Standing right at the end of a line: this press is the one that crosses
    // the break. Everything after it is judged on the next line.
    if (classify(doc->characterAt(pos)) == CharClass::LineBreak)
    {
        if (++pos >= end)
            return end;
    }

    // Swallow whitespace on the way, but never stop inside it - and never
    // past the end of this line, which is a stop of its own.
    while (pos < end && classify(doc->characterAt(pos)) == CharClass::Space)
        ++pos;
    if (pos >= end || classify(doc->characterAt(pos)) == CharClass::LineBreak)
        return pos; // end of the line (trailing blanks crossed), or of the document

    // Then cross exactly one run of same-class characters (a word, or a
    // punctuation/operator run) and stop right where it ends.
    CharClass cls = classify(doc->characterAt(pos));
    int runEnd = pos;
    while (runEnd < end && classify(doc->characterAt(runEnd)) == cls)
        ++runEnd;

    // Exception: a single separator glued directly onto a following word,
    // with no whitespace in between - the '.' in "qwe.rty", the '=' in
    // "a=b" - isn't its own stop. VS Code treats it as glue and jumps
    // straight through it into that word (so "qwe.rty" is two Ctrl+Right
    // presses, not three). A *run* of separators (e.g. "..."), or one
    // bounded by whitespace on the far side (e.g. "foo = bar"), still gets
    // its own stop same as before - only a lone, word-adjacent one merges.
    if (cls == CharClass::Other && runEnd - pos == 1 &&
        runEnd < end && classify(doc->characterAt(runEnd)) == CharClass::Word)
    {
        int wordEnd = runEnd;
        while (wordEnd < end && classify(doc->characterAt(wordEnd)) == CharClass::Word)
            ++wordEnd;
        return wordEnd;
    }

    return runEnd;
}

int WordNav::previousBoundary(const QTextDocument *doc, int pos)
{
    const int end = doc->characterCount() - 1;
    if (pos > end)
        pos = end;
    if (pos <= 0)
        return 0;

    int p = pos - 1;

    // Standing at column 0: this press is the one that crosses the break into
    // the line above, and the search continues from its last character.
    if (classify(doc->characterAt(p)) == CharClass::LineBreak)
    {
        if (--p < 0)
            return 0;
    }

    // Skip whitespace on the way (indentation included), but stop at the
    // line's own start rather than carrying on into the line above.
    while (p > 0 && classify(doc->characterAt(p)) == CharClass::Space)
        --p;

    CharClass cls = classify(doc->characterAt(p));
    if (cls == CharClass::LineBreak)
        return p + 1;                       // start of the line we walked back through
    if (cls == CharClass::Space)
        return 0;                           // ran into the very start of the document

    int runStart = p;
    while (runStart > 0 && classify(doc->characterAt(runStart - 1)) == cls)
        --runStart;

    // Mirror of the forward exception above: a lone separator glued
    // directly onto a preceding word is glue, not its own stop.
    if (cls == CharClass::Other && p == runStart &&
        runStart > 0 && classify(doc->characterAt(runStart - 1)) == CharClass::Word)
    {
        int wordStart = runStart;
        while (wordStart > 0 && classify(doc->characterAt(wordStart - 1)) == CharClass::Word)
            --wordStart;
        return wordStart;
    }

    return runStart;
}
