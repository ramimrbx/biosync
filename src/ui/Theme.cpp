#include "Theme.h"
#include <QApplication>
#include <QStyleHints>
#include <QPalette>
#ifdef Q_OS_WIN
#include <QSettings>
#endif

Theme &Theme::instance() {
    static Theme t;
    return t;
}

void Theme::init() {
    apply();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) { apply(); });
#endif
}

// Whether the OS is currently in dark mode. Reads the same registry value Qt's own Windows
// platform theme plugin does, synchronously — querying qApp->styleHints()->colorScheme() right
// after QApplication is constructed (i.e. before the event loop has run at all) can race the
// platform plugin's own async detection and intermittently report the wrong scheme on first
// launch. The colorSchemeChanged signal (connected in init(), above) is reliable for *live*
// switches, since by then the event loop is running — only the very first read needs this.
static bool osIsDarkMode() {
#ifdef Q_OS_WIN
    QSettings personalize(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        QSettings::NativeFormat);
    if (personalize.contains("AppsUseLightTheme"))
        return personalize.value("AppsUseLightTheme").toInt() == 0;
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;   // Qt < 6.5 (e.g. Ubuntu's Qt 6.4) can't report the OS scheme: use light
#endif
}

// Design tokens mirror ritems/client/web's src/index.css :root variables 1:1 (accent, danger,
// warning are the same hex in both modes there too — only surfaces/borders/text swap), so the
// desktop app reads as the same product as the web client rather than a reskinned lookalike.
void Theme::buildPalette() {
    const bool dark = osIsDarkMode();
    m_palette.isDark = dark;

    m_palette.accent        = QColor("#8222E3");
    m_palette.accentHover   = QColor("#6A1BC4");
    m_palette.accentPressed = QColor("#561499");
    m_palette.onAccent      = QColor("#FFFFFF");

    if (dark) {
        m_palette.pageBg      = QColor("#0F172A");
        m_palette.cardBg      = QColor("#1E293B");
        m_palette.headerBg    = QColor("#0F172A");
        m_palette.border      = QColor("#334155");
        m_palette.borderStrong= QColor("#475569");

        m_palette.textPrimary   = QColor("#F1F5F9");
        m_palette.textSecondary = QColor("#CBD5E1");
        m_palette.textDisabled  = QColor("#64748B");
        m_palette.placeholder   = QColor("#64748B");

        m_palette.success   = QColor("#34D399"); m_palette.successBg = QColor(6, 78, 59, 90);
        m_palette.warning   = QColor("#FBBF24"); m_palette.warningBg = QColor(120, 53, 15, 90);
        m_palette.danger    = QColor("#F87171"); m_palette.dangerBg  = QColor(127, 29, 29, 90);

        m_palette.sidebarBg        = QColor("#0F172A");
        m_palette.sidebarHover     = QColor(30, 41, 59, 153);
        m_palette.sidebarText      = QColor("#E5E7EB");
        m_palette.sidebarTextMuted = QColor("#9CA3AF");

        m_palette.scrollHandle      = QColor("#475569");
        m_palette.scrollHandleHover = m_palette.accent;
        m_palette.shadow            = QColor(0, 0, 0, 160);
    } else {
        m_palette.pageBg      = QColor("#F8FAFC");
        m_palette.cardBg      = QColor("#FFFFFF");
        m_palette.headerBg    = QColor("#F8FAFC");
        m_palette.border      = QColor("#E2E8F0");
        m_palette.borderStrong= QColor("#CBD5E1");

        m_palette.textPrimary   = QColor("#1E293B");
        m_palette.textSecondary = QColor("#475569");
        m_palette.textDisabled  = QColor("#94A3B8");
        m_palette.placeholder   = QColor("#94A3B8");

        m_palette.success   = QColor("#065F46"); m_palette.successBg = QColor("#D1FAE5");
        m_palette.warning   = QColor("#D97706"); m_palette.warningBg = QColor("#FFFBEB");
        m_palette.danger    = QColor("#DC2626"); m_palette.dangerBg  = QColor("#FEF2F2");

        m_palette.sidebarBg        = QColor("#FFFFFF");
        m_palette.sidebarHover     = QColor("#F3F4F6");
        m_palette.sidebarText      = QColor("#111827");
        m_palette.sidebarTextMuted = QColor("#374151");

        m_palette.scrollHandle      = QColor("#CBD5E1");
        m_palette.scrollHandleHover = m_palette.accent;
        m_palette.shadow            = QColor(15, 23, 42, 20);
    }
}

QString Theme::buildStyleSheet() const {
    const ThemePalette &p = m_palette;
    auto hex = [](const QColor &c) { return c.name(QColor::HexRgb); };

    return QString(R"(
        QWidget { color: %textPrimary; }
        QToolTip {
            background: %cardBg; color: %textPrimary; border: 1px solid %border;
            border-radius: 8px; padding: 6px 10px;
        }
        QPushButton {
            background: %cardBg; color: %textSecondary; border: 1.5px solid %border;
            padding: 10px 18px; border-radius: 12px; font-weight: 600; font-size: 13px;
        }
        QPushButton:hover { background: %headerBg; border-color: %borderStrong; }
        QPushButton:pressed { background: %border; }
        QPushButton:disabled { color: %textDisabled; border-color: %border; }
        QPushButton[accent="true"] {
            background: %accent; color: %onAccent; border: none; font-weight: 600;
        }
        QPushButton[accent="true"]:hover { background: %accentHover; }
        QPushButton[accent="true"]:pressed { background: %accentPressed; }
        QPushButton[accent="true"]:disabled { background: %border; color: %textDisabled; }
        QPushButton[danger="true"] { color: %danger; border-color: %danger; }
        QPushButton[danger="true"]:hover { background: %dangerBg; }

        QLineEdit, QSpinBox, QComboBox {
            background: %headerBg; border: 1.5px solid %border; border-radius: 12px;
            padding: 10px 14px; min-height: 20px; font-size: 13px; color: %textPrimary;
            selection-background-color: %accent; selection-color: %onAccent;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %accent; background: %cardBg; }
        QComboBox::drop-down { border: none; padding-right: 8px; }
        QComboBox QAbstractItemView {
            background: %cardBg; border: 1.5px solid %border; border-radius: 12px;
            selection-background-color: %accentSoft; selection-color: %accentHover;
            outline: 0; padding: 6px;
        }

        QTableWidget {
            background: %cardBg; border: 1.5px solid %border; border-radius: 12px;
            gridline-color: transparent; font-size: 13px; outline: 0;
            selection-background-color: %accentSoft; selection-color: %accentHover;
        }
        QTableWidget::item { padding: 8px 12px; border: none; color: %textSecondary; }
        QTableWidget::item:hover { background: %headerBg; }
        QTableWidget::item:selected { background: %accentSoft; color: %accentHover; }
        QHeaderView::section {
            background: %headerBg; color: %textDisabled; border: none;
            border-bottom: 1.5px solid %border; padding: 10px 12px;
            font-weight: 700; font-size: 11px;
        }

        QMenu {
            background: %cardBg; border: 1.5px solid %border; border-radius: 12px; padding: 6px;
        }
        QMenu::item { padding: 8px 20px; border-radius: 8px; font-size: 13px; color: %textPrimary; }
        QMenu::item:selected { background: %accentSoft; color: %accentHover; }
        QMenu::item[danger="true"] { color: %danger; }
        QMenu::separator { height: 1px; background: %border; margin: 4px 8px; }

        QGroupBox {
            background: %cardBg; border: 1.5px solid %border; border-radius: 16px;
            padding: 20px; margin-top: 8px; font-size: 13px; font-weight: 600; color: %textPrimary;
        }
        QGroupBox::title {
            subcontrol-origin: margin; left: 16px; padding: 0 8px;
            background: %cardBg; color: %textPrimary;
        }

        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: %scrollHandle; border-radius: 4px; min-height: 30px; }
        QScrollBar::handle:vertical:hover { background: %scrollHandleHover; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
        QScrollBar::handle:horizontal { background: %scrollHandle; border-radius: 4px; min-width: 30px; }
        QScrollBar::handle:horizontal:hover { background: %scrollHandleHover; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

        QMessageBox, QDialog { background: %pageBg; }
        QStatusBar { background: %sidebarBg; border-top: 1px solid %border; font-size: 11px; color: %textSecondary; }
        QStatusBar::item { border: none; }
    )")
        // Longer/more-specific placeholders must be replaced before their shorter prefixes
        // (e.g. %accentHover before %accent), or the shorter match clobbers part of the longer
        // one's name and leaves a garbage suffix stuck onto the replaced hex value.
        .replace("%accentPressed", hex(p.accentPressed))
        .replace("%accentHover", hex(p.accentHover))
        .replace("%accentSoft", QString("rgba(%1,%2,%3,%4)")
                     .arg(p.accent.red()).arg(p.accent.green()).arg(p.accent.blue())
                     .arg(p.isDark ? 45 : 25))
        .replace("%accent", hex(p.accent))
        .replace("%onAccent", hex(p.onAccent))
        .replace("%pageBg", hex(p.pageBg))
        .replace("%cardBg", hex(p.cardBg))
        .replace("%headerBg", hex(p.headerBg))
        .replace("%borderStrong", hex(p.borderStrong))
        .replace("%border", hex(p.border))
        .replace("%textPrimary", hex(p.textPrimary))
        .replace("%textSecondary", hex(p.textSecondary))
        .replace("%textDisabled", hex(p.textDisabled))
        .replace("%dangerBg", hex(p.dangerBg))
        .replace("%danger", hex(p.danger))
        .replace("%sidebarBg", hex(p.sidebarBg));
}

void Theme::apply() {
    buildPalette();

    QPalette pal;
    pal.setColor(QPalette::Window,          m_palette.pageBg);
    pal.setColor(QPalette::WindowText,      m_palette.textPrimary);
    pal.setColor(QPalette::Base,            m_palette.cardBg);
    pal.setColor(QPalette::AlternateBase,   m_palette.headerBg);
    pal.setColor(QPalette::Text,            m_palette.textPrimary);
    pal.setColor(QPalette::BrightText,      QColor("#FFFFFF"));
    pal.setColor(QPalette::Button,          m_palette.cardBg);
    pal.setColor(QPalette::ButtonText,      m_palette.textPrimary);
    pal.setColor(QPalette::Highlight,       m_palette.accent);
    pal.setColor(QPalette::HighlightedText, m_palette.onAccent);
    pal.setColor(QPalette::ToolTipBase,     m_palette.cardBg);
    pal.setColor(QPalette::ToolTipText,     m_palette.textPrimary);
    pal.setColor(QPalette::PlaceholderText, m_palette.placeholder);
    qApp->setPalette(pal);
    qApp->setStyleSheet(buildStyleSheet());

    emit changed();
}
