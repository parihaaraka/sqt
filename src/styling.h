#ifndef STYLING_H
#define STYLING_H

#include <QColor>

class QPalette;
class QWidget;
class QTableView;

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

/// \a logicalSize rounded so that it covers a whole number of physical pixels on
/// \a w's screen. Give it a table's row height: a size that is integral in
/// logical pixels still lands on a fraction under a fractional scale factor (17
/// at 150% is 25.5), and every second row boundary then falls between two device
/// pixels, so the grid line under it is drawn one pixel thick in some rows and
/// two in others. Windows is where this shows, its usual 125%/150% being exactly
/// the ratios that do it - at 100% and 200% there is nothing to round.
int snapToDevicePixels(int logicalSize, const QWidget *w);

/// The height a single-line row of \a tv needs so that a cell painted by its
/// *current* style - native margins plus whatever `QTableView::item {
/// padding: ... }` the user put in their stylesheet - fits without being
/// pinched, snapped to whole device pixels the same way as
/// snapToDevicePixels().
///
/// A row height guessed as some multiple of the font's height used to stand
/// in for this and matched Qt's own margins closely enough. It stops matching
/// as soon as a user asks for their own padding: the style is then free to
/// reserve more room than the guess assumed, the row is set a few pixels too
/// short for it, and the missing pixels come out of the *bottom* padding -
/// the top padding is drawn in full first, so the text ends up looking glued
/// to the row's top edge rather than merely a bit cramped. Which platform's
/// style happens to need more than the guess provided (Windows, usually) is
/// what makes the very same stylesheet look fine on one OS and broken on
/// another. Asking the style itself, via QStyle::sizeFromContents(), removes
/// the guess and the mismatch along with it.
int comfortableRowHeight(const QTableView *tv);

/// Snaps every current column of \a tv to snapToDevicePixels() of its own
/// width, the same way row heights are snapped, and keeps doing so for
/// columns resized afterwards (by the user dragging a header edge, or by a
/// later resizeColumnsToContents()) for as long as \a tv lives.
///
/// A row height alone only keeps the *horizontal* grid lines crisp. The
/// vertical ones sit at the cumulative sum of column widths, so unless every
/// individual width is itself a whole number of device pixels, that sum
/// drifts in and out of pixel alignment as it grows - not randomly, but with
/// whatever period the widths happen to repeat at, which is exactly the
/// regular "some lines 1px, some 2px" banding a fractional Windows scale
/// (125%, 150%...) produces once the row-height half of this is already
/// fixed.
void keepColumnsSnappedToDevicePixels(QTableView *tv);

/// Paints \a tv's grid lines by hand, in place of `tv->setShowGrid(true)`.
/// Meant to be called from a QEvent::Paint on \a tv's own viewport, before
/// the event reaches the view itself (see AppEventHandler) - Qt draws its
/// grid before the cells, and the cells' own painting is what is meant to
/// cover the leftover pixel of each line inside its cell.
///
/// Exists because QTableView's own grid (a cosmetic QPen) does not reliably
/// come out exactly one physical pixel wide under a fractional display scale
/// on every platform - see comfortableRowHeight() and
/// keepColumnsSnappedToDevicePixels(), which already guarantee every row and
/// column boundary lands on a whole device pixel; this only replaces *how*
/// the line at that boundary gets drawn, not *where*. An earlier version of
/// this function also tried to correct for the viewport's own on-screen
/// position (which an ancestor - a splitter the user dragged, say - places
/// at a logical offset not itself guaranteed to land on a whole device
/// pixel). That correction shifted the grid lines away from the exact
/// coordinates the cells themselves are painted at, and a cell's own
/// selection/background rect then visibly over- or under-shot the line next
/// to it - worse than the line being imperceptibly off was to begin with.
/// Removed rather than chased further without a way to reproduce the
/// original platform-specific symptom directly.
///
/// What replaced it: every line is drawn at the boundary coordinate plus half
/// its own device-pixel width, not at the boundary itself. A cell's own fill
/// stops at that boundary under the ordinary half-open fill rule (it owns
/// [left, boundary), never the boundary pixel itself), so the line is meant
/// to occupy the whole-device-pixel band starting there, [boundary,
/// boundary+width) - and centring a pen of that width on a coordinate half a
/// width past the boundary is what lands it exactly on that band. At width 1
/// this also makes the request unambiguous in principle: a pen centred
/// exactly on the boundary itself is a tie between the pixel this band names
/// and the one before it. In practice the centring alone did not fully fix
/// the Windows-only symptom it was aimed at: a selected cell's background
/// still ended up about one physical pixel off from the grid line next to it
/// there (confirmed still present after that change, on real hardware - it
/// just never reproduces in this project's own Linux sandbox, so there is no
/// local way to keep iterating on it). Decided, together with the person who
/// could actually see it, that this residual is cosmetic enough - a
/// selection edge overlapping its grid line by a pixel, not any loss of data
/// legibility - to leave as a known limitation rather than keep guessing at
/// a platform-specific rounding rule neither of us can step through
/// directly.
///
/// The line's width itself is qBound(1, qRound(devicePixelRatioF()), 4)
/// device pixels, not a flat one - see the comment at that expression in the
/// .cpp for why a fixed one physical pixel, correct at an unscaled ~100 dpi,
/// went from "thin but there" at a laptop's 150% to "all but gone" next to
/// the several-times-larger text a properly scaled dense display puts around
/// it, and why rounding a continuous quantity (devicePixelRatioF() itself)
/// rather than reproducing the old boundary-position tie is what keeps this
/// stable instead of flickering between widths.
void paintPixelPerfectGrid(QTableView *tv);

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
