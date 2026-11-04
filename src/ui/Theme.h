#pragma once
#include <QObject>
#include <QColor>
#include <QString>
#include <QtGlobal>

// Centralized design tokens + global QSS, light and dark, matching ritems/client/web's visual
// language (src/index.css's :root variables + components/ui kit) so BioSync reads as the same
// product as the web client. Follows the OS color scheme (Qt6's QStyleHints::colorScheme(), which
// mirrors Windows' AppsUseLightTheme automatically). Custom-painted widgets (e.g. the sidebar
// NavButton) should read colors from Theme::current() in their paintEvent() rather than
// hardcoding hex values, and connect to Theme::instance().changed() to repaint/re-fetch icons
// when the OS theme flips live.
struct ThemePalette {
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor onAccent;        // text/icon color drawn on top of an accent-filled surface

    QColor pageBg;
    QColor cardBg;
    QColor headerBg;
    QColor border;
    QColor borderStrong;

    QColor textPrimary;
    QColor textSecondary;
    QColor textDisabled;
    QColor placeholder;

    QColor success;    QColor successBg;
    QColor warning;    QColor warningBg;
    QColor danger;     QColor dangerBg;

    QColor sidebarBg;
    QColor sidebarHover;
    QColor sidebarText;
    QColor sidebarTextMuted;

    QColor scrollHandle;
    QColor scrollHandleHover;
    QColor shadow;

    bool isDark = false;

    // ritems' --color-primary-light / --color-primary-ring: a translucent accent wash, used for
    // soft modal headers, selected-row/menu-item backgrounds, and focus rings. alpha01 is 0..1;
    // Qt's QSS rgba() takes an 0-255 alpha (unlike CSS's 0-1), so it's converted here.
    QString accentSoft(double alpha01 = 0.08) const {
        return QString("rgba(%1,%2,%3,%4)")
            .arg(accent.red()).arg(accent.green()).arg(accent.blue())
            .arg(qRound(alpha01 * 255));
    }
};

class Theme : public QObject {
    Q_OBJECT
public:
    static Theme &instance();
    static const ThemePalette &current() { return instance().m_palette; }
    static bool isDark() { return instance().m_palette.isDark; }

    // Call once, right after the QApplication is constructed. Builds the palette/stylesheet for
    // the current OS setting, applies it, and wires live switching.
    void init();

    // Rebuilds the palette + global stylesheet from the current OS color scheme and re-applies
    // them to qApp. Safe to call again any time (e.g. in response to a theme change).
    void apply();

signals:
    // Emitted after apply() runs, whether from init(), a manual call, or a live OS theme flip.
    void changed();

private:
    Theme() = default;
    ThemePalette m_palette;

    void buildPalette();
    QString buildStyleSheet() const;
};
