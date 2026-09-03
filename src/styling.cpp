#include "styling.h"
#include <QColor>
#include <QPalette>
#include <QWidget>
#include <QGuiApplication>
#include <QtMath>
#include <cmath>
#include <QTableView>
#include <QHeaderView>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QAbstractItemModel>
#include <QPainter>


#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
#include <QStyleHints>
#endif

bool isDarkMode()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    return scheme == Qt::ColorScheme::Dark;
#else
    const QPalette defaultPalette;
    const auto text = defaultPalette.color(QPalette::WindowText);
    const auto window = defaultPalette.color(QPalette::Window);
    return text.lightness() > window.lightness();
#endif
}

// WCAG relative luminance, the standard way to reason about "can this be told
// apart from that". A ratio below ~1.6:1 is what makes two colours read as one.
static qreal relativeLuminance(const QColor &c)
{
    auto channel = [](qreal v) {
        return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel(c.redF()) +
           0.7152 * channel(c.greenF()) +
           0.0722 * channel(c.blueF());
}

static qreal contrastRatio(const QColor &a, const QColor &b)
{
    const qreal la = relativeLuminance(a);
    const qreal lb = relativeLuminance(b);
    const qreal hi = qMax(la, lb);
    const qreal lo = qMin(la, lb);
    return (hi + 0.05) / (lo + 0.05);
}

static QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF()   * (1 - t) + b.redF()   * t,
                            a.greenF() * (1 - t) + b.greenF() * t,
                            a.blueF()  * (1 - t) + b.blueF()  * t);
}

QColor readableInactiveHighlight(const QPalette &p)
{
    const QColor base = p.color(QPalette::Inactive, QPalette::Base);
    const QColor inactive = p.color(QPalette::Inactive, QPalette::Highlight);

    // A theme whose unfocused selection already stands out against the text area
    // is left exactly as it is - the whole point of the Inactive group is that a
    // toolkit/theme may have thought this through, and most do.
    if (contrastRatio(inactive, base) >= 1.6)
        return QColor();

    // Derive the replacement from the *active* selection, so it stays a weaker
    // sibling of the focused one rather than an unrelated colour: mixed a good
    // way towards Base, it reads as "selected, but not the focused widget".
    const QColor active = p.color(QPalette::Active, QPalette::Highlight);
    QColor candidate = mix(active, base, 0.55);

    // Guarantee the outcome, whatever the active colour turned out to be: pull it
    // further from Base until it is unmistakably visible, giving up only at the
    // active colour itself (never more prominent than the focused selection).
    for (int i = 0; i < 6 && contrastRatio(candidate, base) < 1.7; ++i)
        candidate = mix(candidate, active, 0.25);

    // Mixing towards active only ever approaches it, so a theme whose focused
    // selection is itself dull leaves us a shade short of even that. Then the
    // active colour is simply the best answer available - the two coincide, which
    // is no loss: an unfocused selection as visible as a focused one is exactly
    // what such a theme gives its focused one too.
    if (contrastRatio(candidate, base) < 1.7 &&
        contrastRatio(active, base) > contrastRatio(candidate, base))
        return active;

    return candidate;
}

namespace
{
    // The same-word mark, all of it: these four numbers are its whole appearance
    // and are meant to be tuned by hand.
    //
    // Three earlier attempts turned a single knob - how strongly a translucent
    // colour washed the page - and all three failed the same way, because a wash
    // trades one thing against the other: whatever contrast it gains against the
    // page it takes from the glyphs sitting on it. So the mark now brings its own
    // foreground and the two are set independently.

    /// How much of the theme's saturation survives, and the ceiling on it. Low,
    /// because the mark should read as "this word again" rather than as a colour
    /// of its own: a soft tinted grey that belongs to the colour scheme, not a
    /// block of the accent.
    constexpr qreal kChromaKeep = 0.35;
    constexpr qreal kChromaCap  = 0.22;

    /// Lightness of the plate on a dark editor; a light one gets 1 - this. The
    /// one knob that matters: the plate is of the opposite polarity to the page,
    /// and this says how far it goes. Too high glares, too low stops reading as
    /// an inversion - 0.66 is a mid-light tint, visible without shouting.
    constexpr qreal kPlateLightness = 0.66;

    /// The glyphs on the plate are the page's own colour, which is what makes the
    /// mark read as a cut-out of the page. Should some palette put the two too
    /// close together, they are pushed apart towards the far pole until they
    /// reach this - comfortable reading, WCAG's AA for body text.
    constexpr qreal kTextOnPlate = 4.5;
}

OccurrenceMark occurrenceMark(const QPalette &p)
{
    const QColor base = p.color(QPalette::Active, QPalette::Base);
    const QColor text = p.color(QPalette::Active, QPalette::Text);
    const QColor highlight = p.color(QPalette::Active, QPalette::Highlight);

    // Which way to skew, taken from the palette in hand rather than from
    // isDarkMode(): the editor is painted with the palette it was given, so a
    // pure function of that palette is both correct for a widget carrying its own
    // colours and something a test can pin down.
    const bool darkEditor = relativeLuminance(base) < relativeLuminance(text);

    // The theme's hue with most of its colourfulness taken out - enough to tell
    // the mark belongs to this colour scheme, not enough to become a colour in its
    // own right. An achromatic or unset Highlight needs no special case:
    // getHslF() reports hue -1 for it, and the result is plain grey.
    float h = 0, s = 0, l = 0;
    highlight.getHslF(&h, &s, &l);
    OccurrenceMark mark;
    mark.background = QColor::fromHslF(h < 0 ? 0.f : h,
                                       qMin(s * float(kChromaKeep), float(kChromaCap)),
                                       float(darkEditor ? kPlateLightness : 1.0 - kPlateLightness));

    // The page's own colour on top: the mark then looks like the page showing
    // through a plate rather than like a second colour scheme. Opaque throughout -
    // with the foreground stated outright there is nothing to gain from letting
    // the syntax colouring show through, and an opaque plate composes predictably
    // over whatever else is painted below (the current-line wash, say).
    mark.foreground = base;

    // A palette where Base and the plate happen to sit close together would leave
    // the words on the mark harder to read than the ones off it, which is the one
    // outcome this design exists to prevent. Then the glyphs give up on matching
    // the page and head for the far pole instead - away from the plate, so still
    // an inversion, just a firmer one.
    if (contrastRatio(mark.foreground, mark.background) < kTextOnPlate)
    {
        const QColor pole = darkEditor ? QColor(Qt::black) : QColor(Qt::white);
        for (int i = 0; i < 20 &&
             contrastRatio(mark.foreground, mark.background) < kTextOnPlate; ++i)
            mark.foreground = mix(mark.foreground, pole, 0.2);
    }

    return mark;
}

void fixInactiveSelection(QWidget *w)
{
    if (!w)
        return;

    // From the application palette rather than the widget's own, and recomputed
    // in full each time: called again on ApplicationPaletteChange, it must not
    // build a correction on top of a previous correction (that would drift
    // brighter every theme toggle). qApp's palette is the pristine one.
    QPalette appPalette = QGuiApplication::palette();
    const QColor better = readableInactiveHighlight(appPalette);
    if (!better.isValid())
    {
        // The theme is fine on its own: make sure any earlier override is gone.
        w->setPalette(QPalette());
        return;
    }

    QPalette pal = appPalette;
    pal.setColor(QPalette::Inactive, QPalette::Highlight, better);
    // The text on top follows the active selection's, which the theme has already
    // paired with its highlight for legibility.
    pal.setColor(QPalette::Inactive, QPalette::HighlightedText,
                 appPalette.color(QPalette::Active, QPalette::HighlightedText));
    w->setPalette(pal);
}

int snapToDevicePixels(int logicalSize, const QWidget *w)
{
    if (!w || logicalSize <= 0)
        return logicalSize;

    const qreal dpr = w->devicePixelRatioF();
    // Nothing to do at an integral ratio, and the guard also covers a screen that
    // reports a ratio of 0 before the widget is shown.
    if (dpr <= 0 || qFuzzyCompare(dpr, qreal(qRound(dpr))))
        return logicalSize;

    // Up to the next size whose device size is whole - never down, since that
    // could reach 0 for a small size, and a row/column one pixel bigger than
    // asked is invisible next to a grid line that changes thickness from one
    // row or column to the next.
    //
    // ceil(ceil(logicalSize*dpr)/dpr) - the previous approach here - finds
    // *a* whole number of device pixels, but nothing guarantees it is the
    // *closest* one reachable from a whole logical size: at dpr=1.25 it maps
    // logical 1 to 2, i.e. 2*1.25=2.5 device pixels - still fractional. Every
    // fractional scale factor Windows and Linux actually offer is some tidy
    // ratio p/q (quarters on Windows: 125/150/175%; other small denominators
    // on Linux's fractional scaling), so the smallest logical size at or above
    // the one asked for that lands on a whole device pixel is never more than
    // q away - a handful of steps, found fastest by just trying them, no
    // reconstruction of p/q from a rounded double required.
    //
    // The comparison against "whole" needs slack rather than qFuzzyCompare's
    // near-exact tolerance: devicePixelRatioF() is a float under the hood, so
    // even a perfectly clean ratio like 1.2 already arrives as something like
    // 1.1999969... - correct to about six decimal digits, not sixteen. A
    // tenth of a device pixel is still far below anything visible, so
    // treating that noise as "whole" costs nothing.
    for (int l = logicalSize; l < logicalSize + 128; ++l)
    {
        const qreal devicePixels = l * dpr;
        if (std::abs(devicePixels - std::round(devicePixels)) < 1e-3)
            return l;
    }
    // Not a realistic scale factor (or a very large denominator): fall back
    // to the exact request rather than looping further.
    return logicalSize;
}

int comfortableRowHeight(const QTableView *tv)
{
    if (!tv)
        return 0;

    // CT_ItemViewItem is precisely the box QStyleSheetStyle overrides to add
    // the ::item rule's border and padding on top of whatever the native
    // style already reserves, so this - rather than a hand-rolled multiple of
    // the font's height - is what stays correct for any padding the user
    // writes and any style it is combined with.
    QStyleOptionViewItem opt;
    opt.initFrom(tv);
    opt.font = tv->font();
    opt.fontMetrics = tv->fontMetrics();
    opt.features = QStyleOptionViewItem::HasDisplay;
    opt.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
    // A representative single line - both an ascender and a descender present,
    // so the measured height isn't accidentally short for glyphs the actual
    // cell content will have but "Ag" itself would lack.
    opt.text = QStringLiteral("Ag");

    const int contentHeight = tv->style()->sizeFromContents(
                QStyle::CT_ItemViewItem, &opt, QSize(), tv).height();
    const int minHeight = tv->verticalHeader()->minimumSectionSize();
    return snapToDevicePixels(qMax(minHeight, contentHeight), tv);
}

void keepColumnsSnappedToDevicePixels(QTableView *tv)
{
    if (!tv)
        return;

    QHeaderView *hh = tv->horizontalHeader();

    // Whatever is already there - typically a resizeColumnsToContents() that
    // ran just before this call.
    for (int i = 0; i < hh->count(); ++i)
    {
        const int w = hh->sectionSize(i);
        const int snapped = snapToDevicePixels(w, tv);
        if (snapped != w)
            hh->resizeSection(i, snapped);
    }

    // And everything resized afterwards, interactively or otherwise. Re-snapping
    // the very size this signal just reported would be an infinite loop; it
    // isn't one, because the second call finds newSize already snapped and
    // does not call resizeSection() again.
    // Connected once per header - a call site can legitimately run this
    // again on the very same long-lived QTableView (ui->tableView is reused
    // across tree navigations, resizeColumnsToContents() and all), and a
    // second connection would just mean every future resize gets snapped
    // twice over.
    static const char *kWiredProperty = "_sqtColumnsSnapped";
    if (hh->property(kWiredProperty).toBool())
        return;
    hh->setProperty(kWiredProperty, true);

    QObject::connect(hh, &QHeaderView::sectionResized, tv,
                      [tv, hh](int logicalIndex, int /*oldSize*/, int newSize)
    {
        const int snapped = snapToDevicePixels(newSize, tv);
        if (snapped != newSize)
            hh->resizeSection(logicalIndex, snapped);
    });
}

void paintPixelPerfectGrid(QTableView *tv)
{
    if (!tv)
        return;

    QAbstractItemModel *model = tv->model();
    if (!model || !model->rowCount() || !model->columnCount())
        return;

    QWidget *vp = tv->viewport();
    const qreal dpr = tv->devicePixelRatioF();
    if (dpr <= 0)
        return;

    const int lastCol = model->columnCount() - 1;
    const int lastRow = model->rowCount() - 1;
    const int firstVisibleCol = qMax(0, tv->columnAt(0));
    const int firstVisibleRow = qMax(0, tv->rowAt(0));
    int lastVisibleCol = tv->columnAt(vp->width());
    if (lastVisibleCol < 0)
        lastVisibleCol = lastCol;
    int lastVisibleRow = tv->rowAt(vp->height());
    if (lastVisibleRow < 0)
        lastVisibleRow = lastRow;

    const int right = tv->columnViewportPosition(lastVisibleCol) + tv->columnWidth(lastVisibleCol);
    const int bottom = tv->rowViewportPosition(lastVisibleRow) + tv->rowHeight(lastVisibleRow);

    QPainter painter(vp);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // The same style hint QTableView's own (now unused, see setShowGrid(false)
    // at every call site) grid used, rather than a guess at a palette role: a
    // generic role like Mid is tuned for widget chrome, not specifically for
    // being visible as a hairline against a themed grid - on a dark theme it
    // all but disappears.
    QStyleOptionViewItem opt;
    opt.initFrom(tv);
    const QColor gridColor = QColor::fromRgba(
                static_cast<QRgb>(tv->style()->styleHint(QStyle::SH_Table_GridLineColor, &opt, tv)));
    QPen pen(gridColor);

    // A fixed one device pixel (what a cosmetic pen gives, and what this used
    // to be unconditionally) is a fine hairline at the ~100 dpi of an
    // unscaled desktop monitor, but it does not grow with everything else
    // once the display scale does: at 150% on a small laptop panel it is
    // already thin enough to be easy to miss, and on a genuinely dense
    // display scaled well past that it all but disappears next to text
    // rendered several times larger. Scaling the line's device-pixel width by
    // the same dpr that already scales the fonts and the rows around it
    // keeps it in the same visual proportion to the rest of the grid at any
    // scale - qRound() rather than dpr itself because the width has to be a
    // whole number of device pixels to stay crisp (see the offset below), and
    // because a fixed width for a given rounded dpr is what keeps it from the
    // one-pixel-here-two-pixels-there flicker a boundary-dependent rounding
    // rule produced before. Bounded well above any scale factor any real
    // display is likely to report, only so a screen misreporting its ratio
    // cannot ask for a grid drawn in bars.
    //
    // pen.setCosmetic(true);
    const int deviceLineWidth = qBound(1, qRound(dpr), 4);
    pen.setWidthF(deviceLineWidth / dpr);

    painter.setPen(pen);

    // Half of the line's own *device*-pixel width past the cell boundary,
    // converted back to the logical units this painter is given coordinates
    // in - not the boundary itself. A cell's own fill (drawn separately,
    // right after this) covers device columns/rows [left, boundary) under
    // the ordinary half-open fill rule, so the line is meant to occupy the
    // whole-device-pixel band starting there, [boundary, boundary+width) -
    // and centring a pen of that same width on a coordinate half its width
    // past the boundary is what lands it exactly on that band and nowhere
    // else. At width 1 this is also what makes the request unambiguous: a
    // one-pixel pen centred exactly on the boundary coordinate is a tie
    // between the pixel this band names and the one before it, and which way
    // a given paint backend breaks that tie is not something worth depending
    // on - it is exactly the kind of platform-specific rounding rule that
    // otherwise puts the line a pixel away from where the cells think their
    // own edge is.
    //
    // const qreal half = dpr > 0 ? 0.5 / dpr : 0.0;
    const qreal half = dpr > 0 ? (deviceLineWidth / 2.0) / dpr : 0.0;

    // Deliberately at the exact same coordinates QTableView itself paints
    // cell content at (columnViewportPosition()+columnWidth(), etc.) rather
    // than at some position corrected for where an ancestor happens to have
    // placed the viewport: a correction here alone, with nothing painting the
    // cells themselves any differently, only pulls the grid line away from
    // the selection/background rect that is drawn at the raw position - a
    // cell's highlight then over- or under-shoots the line next to it, which
    // is worse than the line itself being imperceptibly off.
    //
    // Columns are assumed to appear in logical order - true as long as
    // nothing calls setSectionsMovable(true) on the horizontal header.
    for (int col = firstVisibleCol; col <= lastVisibleCol; ++col)
    {
        const qreal x = tv->columnViewportPosition(col) + tv->columnWidth(col) + half;
        painter.drawLine(QPointF(x, 0), QPointF(x, bottom));
    }
    for (int row = firstVisibleRow; row <= lastVisibleRow; ++row)
    {
        const qreal y = tv->rowViewportPosition(row) + tv->rowHeight(row) + half;
        painter.drawLine(QPointF(0, y), QPointF(right, y));
    }
}
