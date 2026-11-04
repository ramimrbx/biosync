#include "Icons.h"
#include <QFile>
#include <QPainter>
#include <QSvgRenderer>
#include <QHash>

namespace Icons {

static QString resourcePath(Glyph g) {
    switch (g) {
        case Glyph::Refresh:      return ":/icons/refresh.svg";
        case Glyph::Retry:        return ":/icons/retry.svg";
        case Glyph::Devices:      return ":/icons/devices.svg";
        case Glyph::Attendance:   return ":/icons/attendance.svg";
        case Glyph::Logs:         return ":/icons/logs.svg";
        case Glyph::Settings:     return ":/icons/settings.svg";
        case Glyph::Add:          return ":/icons/add.svg";
        case Glyph::Edit:         return ":/icons/edit.svg";
        case Glyph::Delete:       return ":/icons/delete.svg";
        case Glyph::Play:         return ":/icons/play.svg";
        case Glyph::Pause:        return ":/icons/pause.svg";
        case Glyph::Restart:      return ":/icons/restart.svg";
        case Glyph::Upload:       return ":/icons/upload.svg";
        case Glyph::Download:     return ":/icons/download.svg";
        case Glyph::ChevronRight: return ":/icons/chevron_right.svg";
        case Glyph::Warning:      return ":/icons/warning.svg";
        case Glyph::Success:      return ":/icons/success.svg";
        case Glyph::Error:        return ":/icons/error.svg";
        case Glyph::Pending:      return ":/icons/pending.svg";
        case Glyph::Online:       return ":/icons/online.svg";
        case Glyph::Offline:      return ":/icons/offline.svg";
        case Glyph::Close:        return ":/icons/close.svg";
        case Glyph::Info:         return ":/icons/info.svg";
        case Glyph::Monitor:      return ":/icons/monitor.svg";
        case Glyph::Search:       return ":/icons/search.svg";
        case Glyph::TrayShow:     return ":/icons/tray_show.svg";
        case Glyph::Quit:         return ":/icons/quit.svg";
    }
    return {};
}

struct CacheKey {
    Glyph glyph; QRgb color; int px; int dprMilli;
    bool operator==(const CacheKey &o) const {
        return glyph == o.glyph && color == o.color && px == o.px && dprMilli == o.dprMilli;
    }
};
static size_t qHash(const CacheKey &k, size_t seed = 0) {
    return ::qHash(static_cast<int>(k.glyph), seed) ^ ::qHash(k.color) ^
           ::qHash(k.px) ^ ::qHash(k.dprMilli);
}

QPixmap pixmap(Glyph glyph, const QColor &color, int px, qreal dpr) {
    static QHash<CacheKey, QPixmap> cache;
    const CacheKey key{glyph, color.rgba(), px, int(dpr * 1000)};
    auto it = cache.constFind(key);
    if (it != cache.constEnd()) return it.value();

    QFile f(resourcePath(glyph));
    QPixmap result(px, px);
    result.fill(Qt::transparent);
    if (f.open(QIODevice::ReadOnly)) {
        QByteArray svg = f.readAll();
        svg.replace("#FF00FF", color.name(QColor::HexRgb).toUtf8());

        QSvgRenderer renderer(svg);
        const int physPx = qRound(px * dpr);
        QPixmap pm(physPx, physPx);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        renderer.render(&p, QRectF(0, 0, physPx, physPx));
        p.end();
        pm.setDevicePixelRatio(dpr);
        result = pm;
    }
    cache.insert(key, result);
    return result;
}

QIcon icon(Glyph glyph, const QColor &color, int px, qreal dpr) {
    return QIcon(pixmap(glyph, color, px, dpr));
}

} // namespace Icons
