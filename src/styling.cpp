#include "styling.h"
#include <QColor>
#include <QPalette>
#include <QWidget>
#include <QGuiApplication>
#include <QtMath>


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
    // The same-word mark, all of it: these five numbers are its whole appearance
    // and are meant to be tuned by hand.
    //
    // Two earlier attempts turned a single knob - the alpha of the theme's
    // selection colour - and both looked wrong for the same reason: alpha moves
    // visibility and colourfulness together. Over Ubuntu's orange accent that
    // gives either a muddy brown indistinguishable from the page, or a rust
    // block. So the channels are split here: colourfulness is cut on purpose,
    // and the visibility comes from lightness instead.

    /// How much of the theme's saturation survives, and the ceiling on it. Low,
    /// because the mark should read as "this word again" rather than as a colour
    /// of its own - and it lands on text the highlighter has already coloured.
    constexpr qreal kChromaKeep = 0.35;
    constexpr qreal kChromaCap  = 0.20;

    /// Lightness of the tint on a dark editor; a light one gets 1 - this. The
    /// skew that does the visible work: a dark page is marked with something
    /// lighter than itself, a light page with something darker.
    constexpr qreal kTintLightness = 0.85;

    /// How far the composed background must end up from Base - the one number
    /// that decides "can I see it". It also bounds what the glyphs on top can
    /// lose, since a shift of X:1 can cost the text no more than the same X.
    constexpr qreal kTargetShift = 1.50;

    /// Never wash the text out, whatever the palette: an upper bound on alpha in
    /// case some theme makes the target unreachable.
    constexpr qreal kAlphaCap = 0.50;
}

QColor occurrenceMark(const QPalette &p)
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
    // the mark belongs to this colour scheme, not enough to argue with the syntax
    // colouring underneath. An achromatic or unset Highlight needs no special
    // case: getHslF() reports hue -1 for it, and the result is plain grey.
    float h = 0, s = 0, l = 0;
    highlight.getHslF(&h, &s, &l);
    const QColor tint = QColor::fromHslF(h < 0 ? 0.f : h,
                                         qMin(s * float(kChromaKeep), float(kChromaCap)),
                                         float(darkEditor ? kTintLightness : 1.0 - kTintLightness));

    // Then the *smallest* alpha that carries the background as far as asked -
    // smallest, so as much of the highlighter's work shows through as possible.
    // (The old hard-coded yellow sat at 0.7 and buried the glyphs completely.)
    qreal alpha = 0.02;
    while (alpha < kAlphaCap &&
           contrastRatio(mix(base, tint, alpha), base) < kTargetShift)
        alpha += 0.01;

    QColor mark = tint;
    mark.setAlphaF(alpha);
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
