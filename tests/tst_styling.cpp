#include <QtTest>
#include <QPalette>
#include <QWidget>
#include <QtMath>
#include "styling.h"

/// readableInactiveHighlight(): the colour a selection gets while its widget has
/// no focus. Themes differ wildly here - some paint it a hair away from the text
/// background, which hides the very thing the file search puts there (the pane
/// marks the hit, the focus stays in the results tree). The rules the correction
/// must obey are few but easy to break by tweaking the constants, hence this.
class TestStyling : public QObject
{
    Q_OBJECT
private:
    /// WCAG relative luminance, recomputed independently of the implementation.
    static qreal luminance(const QColor &c)
    {
        auto channel = [](qreal v) {
            return v <= 0.03928 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(c.redF()) +
               0.7152 * channel(c.greenF()) +
               0.0722 * channel(c.blueF());
    }
    static qreal contrast(const QColor &a, const QColor &b)
    {
        const qreal la = luminance(a), lb = luminance(b);
        return (qMax(la, lb) + 0.05) / (qMin(la, lb) + 0.05);
    }
    static QPalette makePalette(const QColor &base, const QColor &inactive, const QColor &active)
    {
        QPalette p;
        p.setColor(QPalette::Inactive, QPalette::Base, base);
        p.setColor(QPalette::Inactive, QPalette::Highlight, inactive);
        p.setColor(QPalette::Active, QPalette::Highlight, active);
        p.setColor(QPalette::Active, QPalette::HighlightedText, QColor("#ffffff"));
        return p;
    }
    static QPalette editorPalette(const QColor &base, const QColor &text, const QColor &highlight)
    {
        QPalette p;
        p.setColor(QPalette::Active, QPalette::Base, base);
        p.setColor(QPalette::Active, QPalette::Text, text);
        p.setColor(QPalette::Active, QPalette::Highlight, highlight);
        return p;
    }

private slots:
    void readablePaletteIsLeftAlone_data();
    void readablePaletteIsLeftAlone();
    void invisibleSelectionIsCorrected_data();
    void invisibleSelectionIsCorrected();
    void correctionIsStable();
    void widgetOverrideDoesNotCompound();
    void occurrenceMarkIsAnInversion_data();
    void occurrenceMarkIsAnInversion();
};

void TestStyling::readablePaletteIsLeftAlone_data()
{
    QTest::addColumn<QColor>("base");
    QTest::addColumn<QColor>("inactive");
    QTest::addColumn<QColor>("active");

    // Whatever the theme has thought through stays as it is - the Inactive group
    // exists for the theme to use, and most do it well.
    QTest::newRow("grey on white") << QColor("#ffffff") << QColor("#b0b0b0") << QColor("#3584e4");
    QTest::newRow("grey on black") << QColor("#1e1e1e") << QColor("#5a5a5a") << QColor("#264f78");
    QTest::newRow("same as active") << QColor("#ffffff") << QColor("#3584e4") << QColor("#3584e4");
}

void TestStyling::readablePaletteIsLeftAlone()
{
    QFETCH(QColor, base);
    QFETCH(QColor, inactive);
    QFETCH(QColor, active);
    QVERIFY(!readableInactiveHighlight(makePalette(base, inactive, active)).isValid());
}

void TestStyling::invisibleSelectionIsCorrected_data()
{
    QTest::addColumn<QColor>("base");
    QTest::addColumn<QColor>("inactive");
    QTest::addColumn<QColor>("active");

    // Windows' own dark mode is the case that started this: the unfocused
    // selection is all but the background.
    QTest::newRow("dark") << QColor("#1e1e1e") << QColor("#252525") << QColor("#264f78");
    QTest::newRow("light") << QColor("#ffffff") << QColor("#fafafa") << QColor("#3584e4");
    QTest::newRow("identical to base") << QColor("#2b2b2b") << QColor("#2b2b2b") << QColor("#4a90d9");
    // A theme whose *focused* selection is itself nearly invisible: there is
    // nothing to derive a visible colour from, so the best available answer is
    // the active colour - see the cap asserted below.
    QTest::newRow("dull active") << QColor("#ffffff") << QColor("#fdfdfd") << QColor("#e8e8e8");
}

void TestStyling::invisibleSelectionIsCorrected()
{
    QFETCH(QColor, base);
    QFETCH(QColor, inactive);
    QFETCH(QColor, active);

    const QColor fixed = readableInactiveHighlight(makePalette(base, inactive, active));
    QVERIFY(fixed.isValid());

    // Visible against the text area - but never more prominent than the focused
    // selection, or the eye would be drawn to the widget that is not being typed
    // in. Where the theme's own focused selection is dull to begin with, the cap
    // wins and the two simply coincide: nothing better exists to derive.
    const qreal want = qMin(qreal(1.7), contrast(active, base));
    QVERIFY2(contrast(fixed, base) >= want - 0.001,
             qPrintable(QString("%1 gives contrast %2, wanted %3")
                        .arg(fixed.name()).arg(contrast(fixed, base)).arg(want)));
    QVERIFY(contrast(fixed, base) <= contrast(active, base) + 0.001);

    // And it must actually differ from what the theme offered, otherwise the
    // whole exercise changed nothing.
    QVERIFY(contrast(fixed, base) > contrast(inactive, base));
}

void TestStyling::correctionIsStable()
{
    // Feeding a corrected palette back in must be a no-op: the theme can change
    // at runtime and every widget recomputes, so a correction that kept building
    // on itself would drift brighter with each toggle.
    const QPalette broken = makePalette(QColor("#1e1e1e"), QColor("#252525"), QColor("#264f78"));
    const QColor fixed = readableInactiveHighlight(broken);
    QVERIFY(fixed.isValid());

    QPalette corrected = broken;
    corrected.setColor(QPalette::Inactive, QPalette::Highlight, fixed);
    QVERIFY(!readableInactiveHighlight(corrected).isValid());
}

void TestStyling::widgetOverrideDoesNotCompound()
{
    // fixInactiveSelection() derives everything from the application palette, so
    // calling it twice (construction, then a theme change) must leave the widget
    // in the same state - whichever theme the test happens to run under.
    QWidget w;
    fixInactiveSelection(&w);
    const QColor once = w.palette().color(QPalette::Inactive, QPalette::Highlight);
    fixInactiveSelection(&w);
    QCOMPARE(w.palette().color(QPalette::Inactive, QPalette::Highlight), once);
}

void TestStyling::occurrenceMarkIsAnInversion_data()
{
    QTest::addColumn<QColor>("base");
    QTest::addColumn<QColor>("text");
    QTest::addColumn<QColor>("highlight");

    QTest::newRow("dark") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#264f78");
    QTest::newRow("light") << QColor("#ffffff") << QColor("#202020") << QColor("#3584e4");
    // A dark page that is dark *grey* rather than black - the case the complaint
    // came from, where a translucent wash has the least room to work in.
    QTest::newRow("dark grey page") << QColor("#2b2b2b") << QColor("#d4d4d4") << QColor("#e95420");
    // Ubuntu's orange accent, on both polarities: washing the page with the
    // accent itself used to give either mud or rust.
    QTest::newRow("ubuntu dark") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#e95420");
    QTest::newRow("ubuntu light") << QColor("#ffffff") << QColor("#202020") << QColor("#e95420");
    // Accents that carry no contrast of their own against the page. They needed a
    // special case back when the mark was the accent washed over the page; now
    // that only the hue is borrowed, they are ordinary inputs.
    QTest::newRow("dark, dim highlight") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#232323");
    QTest::newRow("light, pale highlight") << QColor("#ffffff") << QColor("#202020") << QColor("#f4f4f4");
    // The degenerate one: Highlight *is* the text background - no hue to borrow,
    // so the plate comes out a plain grey, which is fine.
    QTest::newRow("highlight equals base") << QColor("#2b2b2b") << QColor("#cccccc") << QColor("#2b2b2b");
    // And a fully achromatic accent, where getHslF() reports hue -1.
    QTest::newRow("grey accent") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#808080");
    // Low-contrast palettes, where the page and its text are closer together than
    // any theme should put them. Nothing here may divide by that closeness.
    QTest::newRow("murky dark") << QColor("#2b2b2b") << QColor("#6a6a6a") << QColor("#e95420");
    QTest::newRow("murky light") << QColor("#f0f0f0") << QColor("#a0a0a0") << QColor("#3584e4");
    // A mid-grey page: dark enough to count as a dark editor, light enough that
    // the plate lands within 4.5:1 of it - which is precisely when the glyphs stop
    // being able to be the page's own colour and the fallback in occurrenceMark()
    // has to push them towards black. Without a row like this the fallback is dead
    // code as far as the test is concerned: replacing its condition with `false`
    // left all the other rows passing.
    QTest::newRow("mid-grey page") << QColor("#4a4a4a") << QColor("#e0e0e0") << QColor("#e95420");
}

void TestStyling::occurrenceMarkIsAnInversion()
{
    QFETCH(QColor, base);
    QFETCH(QColor, text);
    QFETCH(QColor, highlight);

    const OccurrenceMark mark = occurrenceMark(editorPalette(base, text, highlight));

    // The mark is three separate decisions (see occurrenceMark), and there is one
    // group of assertions per decision. Note what is *not* asserted: any
    // relationship to `text`. That is the whole point of the design - the glyphs
    // on the plate are the mark's own, so legibility no longer depends on the
    // colour the word happened to have. The earlier translucent versions had to
    // check exactly that, and could never satisfy it and visibility at once.

    // 1. Polarity: the plate is on the opposite side of the page from the text.
    // A dark editor gets a plate lighter than its page, a light one gets a darker
    // plate. This is what makes the mark visible without touching its opacity.
    const bool darkEditor = luminance(base) < luminance(text);
    QVERIFY2(darkEditor ? luminance(mark.background) > luminance(base)
                        : luminance(mark.background) < luminance(base),
             qPrintable(QString("%1 editor: plate %2 is on the wrong side of Base %3")
                        .arg(darkEditor ? "dark" : "light")
                        .arg(mark.background.name()).arg(base.name())));

    // 2. The two contrasts that decide whether it works at all: the plate has to
    // be findable against the page, and the glyphs have to be readable on the
    // plate. Being independent is exactly what the split buys - both hold at once
    // here, which no single-knob wash could manage.
    QVERIFY2(contrast(mark.background, base) >= 3.0,
             qPrintable(QString("plate %1 against page %2 is only %3:1 - hard to find")
                        .arg(mark.background.name()).arg(base.name())
                        .arg(contrast(mark.background, base))));
    QVERIFY2(contrast(mark.foreground, mark.background) >= 4.5,
             qPrintable(QString("glyphs %1 on plate %2 are only %3:1 - hard to read")
                        .arg(mark.foreground.name()).arg(mark.background.name())
                        .arg(contrast(mark.foreground, mark.background))));

    // The mark stays a mark and does not become a light source: a plate at full
    // white on a dark page glares, and the tuning that arrived at kPlateLightness
    // was mostly about staying below this.
    QVERIFY2(contrast(mark.background, base) <= 12.0,
             qPrintable(QString("plate %1 is %2:1 off the page - a glare, not a mark")
                        .arg(mark.background.name()).arg(contrast(mark.background, base))));

    // 3. Colourfulness, deliberately low: the plate carries a trace of the
    // theme's hue, never the accent itself, so it reads as "this word again"
    // rather than as a colour with a meaning of its own.
    QVERIFY2(mark.background.hslSaturationF() <= 0.23,
             qPrintable(QString("plate %1 keeps saturation %2 - too colourful")
                        .arg(mark.background.name()).arg(mark.background.hslSaturationF())));
    QVERIFY2(mark.background.hslSaturationF() <= highlight.hslSaturationF() + 0.001,
             qPrintable(QString("plate is more saturated (%1) than the theme's accent (%2)")
                        .arg(mark.background.hslSaturationF()).arg(highlight.hslSaturationF())));

    // Both colours are opaque. The plate is painted over whatever is already
    // there (the current-line wash, for one), so translucency here would make the
    // result depend on what it happens to land on - the very thing this replaced.
    QCOMPARE(mark.background.alphaF(), 1.0f);
    QCOMPARE(mark.foreground.alphaF(), 1.0f);

    // 4. The glyphs are the page's own colour - that is what makes the mark read
    // as a cut-out of the page rather than as a second colour scheme - unless the
    // palette leaves them too close to the plate to read, in which case they head
    // for the far pole instead. Both branches are exercised by the rows above.
    if (contrast(base, mark.background) >= 4.5)
    {
        QCOMPARE(mark.foreground, base);
    }
    else
    {
        // Still an inversion, just a firmer one: away from the plate, i.e. past
        // the page rather than back across it.
        QVERIFY2(darkEditor ? luminance(mark.foreground) <= luminance(base)
                            : luminance(mark.foreground) >= luminance(base),
                 qPrintable(QString("glyphs %1 went the wrong way from page %2")
                            .arg(mark.foreground.name()).arg(base.name())));
    }
}

QTEST_MAIN(TestStyling)
#include "tst_styling.moc"
