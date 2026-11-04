#pragma once
#include <QIcon>
#include <QColor>

// Hand-authored Fluent-style SVG icon set (src/assets/icons/*.svg), recolored at load time
// (each SVG uses a #FF00FF sentinel for its themeable fill/stroke) and rasterized at the
// widget's actual device pixel ratio so every icon stays crisp at any Windows DPI scale.
namespace Icons {

enum class Glyph {
    Refresh, Retry, Devices, Attendance, Logs, Settings,
    Add, Edit, Delete, Play, Pause, Restart,
    Upload, Download, ChevronRight, Warning, Success, Error, Pending,
    Online, Offline, Close, Info, Monitor, Search, TrayShow, Quit
};

// Renders the glyph tinted with `color` at `px` logical pixels, sized for `dpr` (pass the
// target widget/screen's devicePixelRatio(); defaults to 1.0 for callers that don't care).
QIcon icon(Glyph glyph, const QColor &color, int px = 16, qreal dpr = 1.0);

// Convenience: a QPixmap instead of a QIcon (e.g. for QLabel-based status pills).
QPixmap pixmap(Glyph glyph, const QColor &color, int px = 16, qreal dpr = 1.0);

} // namespace Icons
