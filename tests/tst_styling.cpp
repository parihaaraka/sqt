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
    /// What the wash actually looks like once composed over the background it
    /// is drawn on - the only thing the eye ever sees of it.
    static QColor composed(const QColor &over, const QColor &translucent)
    {
        const qreal a = translucent.alphaF();
        return QColor::fromRgbF(over.redF()   * (1 - a) + translucent.redF()   * a,
                                over.greenF() * (1 - a) + translucent.greenF() * a,
                                over.blueF()  * (1 - a) + translucent.blueF()  * a);
    }


private slots:
    void readablePaletteIsLeftAlone_data();
    void readablePaletteIsLeftAlone();
    void invisibleSelectionIsCorrected_data();
    void invisibleSelectionIsCorrected();
    void correctionIsStable();
    void widgetOverrideDoesNotCompound();
    void occurrenceMarkStaysReadable_data();
    void occurrenceMarkStaysReadable();
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

void TestStyling::occurrenceMarkStaysReadable_data()
{
    QTest::addColumn<QColor>("base");
    QTest::addColumn<QColor>("text");
    QTest::addColumn<QColor>("highlight");

    QTest::newRow("dark") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#264f78");
    QTest::newRow("light") << QColor("#ffffff") << QColor("#202020") << QColor("#3584e4");
    // Ubuntu's orange accent: the case the complaint came from, where washing the
    // page with the accent itself gave either mud or rust.
    QTest::newRow("ubuntu dark") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#e95420");
    QTest::newRow("ubuntu light") << QColor("#ffffff") << QColor("#202020") << QColor("#e95420");
    // Accents that carry no contrast of their own against the page. These used to
    // need a special case; now that the visibility comes from lightness rather
    // than from the accent, they are ordinary inputs.
    QTest::newRow("dark, dim highlight") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#232323");
    QTest::newRow("light, pale highlight") << QColor("#ffffff") << QColor("#202020") << QColor("#f4f4f4");
    // The degenerate one: Highlight *is* the text background - no hue at all to
    // borrow, so the mark comes out grey, which is fine.
    QTest::newRow("highlight equals base") << QColor("#2b2b2b") << QColor("#cccccc") << QColor("#2b2b2b");
    // And a fully achromatic accent, where getHslF() reports hue -1.
    QTest::newRow("grey accent") << QColor("#1e1e1e") << QColor("#d4d4d4") << QColor("#808080");
}

void TestStyling::occurrenceMarkStaysReadable()
{
    QFETCH(QColor, base);
    QFETCH(QColor, text);
    QFETCH(QColor, highlight);

    const QColor mark = occurrenceMark(editorPalette(base, text, highlight));
    const QColor washed = composed(base, mark);

    // The mark is built from three separate decisions (see occurrenceMark), and
    // there is one assertion per decision.

    // 1. The skew, which is what makes the mark visible at all: a dark page is
    // marked with something lighter than itself, a light page with something
    // darker. Two earlier versions instead washed the page with the theme's
    // accent as-is, which on a dark theme meant a dark brown that vanished into
    // the background - the complaint that led here.
    const bool darkEditor = luminance(base) < luminance(text);
    QVERIFY2(darkEditor ? luminance(washed) > luminance(base)
                        : luminance(washed) < luminance(base),
             qPrintable(QString("%1 editor: mark composes to %2, on the wrong side of Base %3")
                        .arg(darkEditor ? "dark" : "light")
                        .arg(washed.name()).arg(base.name())));

    // 2. Visibility, as a band. The floor is the point of the mark; the ceiling
    // keeps it from growing into a highlighter stroke again, and bounds what the
    // glyphs on top can lose - a shift of X:1 costs the text no more than X,
    // hence the 1/1.6 = 0.62 factor below rather than a second free constant.
    QVERIFY2(contrast(washed, base) >= 1.3,
             qPrintable(QString("mark %1 over %2 composes to %3 - only a %4:1 shift, invisible")
                        .arg(mark.name(QColor::HexArgb)).arg(base.name())
                        .arg(washed.name()).arg(contrast(washed, base))));
    QVERIFY2(contrast(washed, base) <= 1.6,
             qPrintable(QString("mark shifts the background by %1:1 - too loud")
                        .arg(contrast(washed, base))));
    QVERIFY2(contrast(text, washed) >= contrast(text, base) * 0.62,
             qPrintable(QString("text contrast falls from %1 to %2")
                        .arg(contrast(text, base)).arg(contrast(text, washed))));

    // 3. Colourfulness, deliberately low: the mark carries a trace of the theme's
    // hue, never its full accent. This is what keeps it from arguing with the
    // syntax colouring it lands on.
    QVERIFY2(mark.hslSaturationF() <= 0.21,
             qPrintable(QString("mark %1 keeps saturation %2 - too colourful")
                        .arg(mark.name(QColor::HexArgb)).arg(mark.hslSaturationF())));
    QVERIFY2(mark.hslSaturationF() <= highlight.hslSaturationF() + 0.001,
             qPrintable(QString("mark is more saturated (%1) than the theme's accent (%2)")
                        .arg(mark.hslSaturationF()).arg(highlight.hslSaturationF())));

    // A wash, not a fill: the text underneath shows through rather than being
    // replaced by a block of colour. The alpha stays low precisely because the
    // lightness skew, not the alpha, is doing the work.
    QVERIFY(mark.alphaF() > 0.02);
    QVERIFY(mark.alphaF() < 0.5);
}

QTEST_MAIN(TestStyling)
#include "tst_styling.moc"
