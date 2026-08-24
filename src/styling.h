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

/// How the editor marks the other occurrences of the word under the cursor: a
/// pale translucent wash and nothing else, the way VS Code does it - an
/// underline as well read as clutter.
///
/// Derived entirely from \a p (the hue from Highlight, the strength from Base),
/// so a theme swap carries the mark along instead of leaving the hard-coded
/// yellow that used to turn brown keywords into mustard on a dark theme.
///
/// Visibility comes from *lightness*, skewed by the polarity of \a p itself (a
/// dark editor is marked with something lighter than its page, a light one with
/// something darker), while the colourfulness is deliberately cut down towards
/// grey - only a trace of the theme's hue is kept. Turning the alpha of the
/// theme's accent up and down instead moves both at once, which over Ubuntu's
/// orange gives a choice between a muddy brown that vanishes and a rust block.
///
/// See the constants at the top of the implementation: they are the whole
/// appearance of the mark, in one place, meant for tuning by hand.
QColor occurrenceMark(const QPalette &p);

#endif // STYLING_H
