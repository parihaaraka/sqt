#include "styling.h"
#include <QPalette>
#include <QGuiApplication>

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
    return text.stylinglightness() > window.lightness();
#endif
}
