#ifndef STYLING_H
#define STYLING_H

#include <QColor>

class QPalette;
class QWidget;

bool isDarkMode();

/// A visible substitute for \a p's Inactive/Highlight, or an invalid colour when
/// the theme's own one is fine. Windows dark mode paints the unfocused selection
/// nearly the colour of Base, which makes a selection in a widget that does not
/// hold the focus impossible to find - and browsing search results means exactly
/// that: the place is marked in the pane, the focus is in the results tree.
QColor readableInactiveHighlight(const QPalette &p);

/// Applies readableInactiveHighlight() to \a w. A no-op on a theme whose own
/// inactive selection is readable, so nothing is imposed on a sane palette.
/// Recomputed from the application palette every time, hence safe to call again
/// on QEvent::ApplicationPaletteChange without the corrections compounding.
void fixInactiveSelection(QWidget *w);

/// The two colours of the same-word mark: an opaque plate and the glyph colour
/// to put on it.
struct OccurrenceMark
{
    QColor background;
    QColor foreground;
};

/// How the editor marks the other occurrences of the word under the cursor: as
/// an inversion of the page - a soft plate of the *opposite* polarity, carrying
/// glyphs the colour of the page itself. On a dark editor that means dark text
/// on a light plate, and the other way round on a light one.
///
/// Three earlier attempts all washed the page with a translucent colour and left
/// the syntax colouring to show through, and all three failed for one structural
/// reason: a wash has a single knob, and shifting the background by X:1 costs the
/// glyphs on top no less than the same X. Either the mark could not be seen or
/// the words under it could not be read - and on a dark *grey* page (not black)
/// there is even less room to trade. Giving the mark its own foreground breaks
/// that tie: legibility stops depending on the colour the word happened to be.
///
/// The price, paid knowingly, is that the highlighter's colour is replaced
/// inside the mark. That is a matter of appearance, not of information: the word
/// is by definition the one under the cursor, whose colouring is visible at the
/// cursor itself.
///
/// Derived entirely from \a p, so a theme swap carries the mark along: the hue
/// comes from Highlight (the system selection colour, with most of its
/// colourfulness taken out - a soft tinted grey rather than the accent itself),
/// the polarity from Base against Text, the glyph colour from Base.
///
/// See the constants at the top of the implementation: they are the whole
/// appearance of the mark, in one place, meant for tuning by hand.
OccurrenceMark occurrenceMark(const QPalette &p);

#endif // STYLING_H
