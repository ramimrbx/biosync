#include "SplashScreen.h"
#include "Theme.h"
#include <QApplication>
#include <QScreen>
#include <QCursor>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QIcon>
#include <QFont>
#include <cmath>

static constexpr int kCardW = 380;
static constexpr int kCardH = 230;
static constexpr int kMargin = 24; // room around the card for the drop shadow

SplashScreen::SplashScreen(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   Qt::Tool | Qt::SplashScreen);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(kCardW + kMargin * 2, kCardH + kMargin * 2);

    auto *shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(36);
    shadow->setOffset(0, 10);
    shadow->setColor(Theme::current().shadow);
    setGraphicsEffect(shadow);

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect avail = screen->availableGeometry();
        move(avail.center() - rect().center());
    }

    m_spinTimer = new QTimer(this);
    connect(m_spinTimer, &QTimer::timeout, this, [this] {
        m_spinAngle = std::fmod(m_spinAngle + 8.0, 360.0);
        update();
    });
    m_spinTimer->start(16);
}

void SplashScreen::paintEvent(QPaintEvent *) {
    const ThemePalette &t = Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF card(kMargin, kMargin, kCardW, kCardH);
    QPainterPath path;
    path.addRoundedRect(card, 16, 16);
    p.fillPath(path, t.cardBg);

    QPen border(t.border);
    border.setWidthF(1.0);
    p.setPen(border);
    p.drawPath(path);

    // App icon
    const QIcon appIcon(":/assets/images/icon.ico");
    const int iconSize = 56;
    const QRect iconRect(card.left() + (card.width() - iconSize) / 2, card.top() + 30,
                          iconSize, iconSize);
    appIcon.paint(&p, iconRect);

    // Wordmark, brand gradient
    QFont title = font();
    title.setPointSize(20);
    title.setWeight(QFont::Bold);
    p.setFont(title);
    QLinearGradient grad(card.left(), 0, card.right(), 0);
    grad.setColorAt(0, t.accent);
    grad.setColorAt(1, t.accentHover);
    p.setPen(QPen(QBrush(grad), 1));
    const QRectF titleRect(card.left(), iconRect.bottom() + 12, card.width(), 30);
    p.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, "BioSync");

    QFont sub = font();
    sub.setPointSize(10);
    p.setFont(sub);
    p.setPen(t.textSecondary);
    const QRectF subRect(card.left(), titleRect.bottom(), card.width(), 20);
    p.drawText(subRect, Qt::AlignHCenter | Qt::AlignVCenter, "Attendance Manager");

    // Indeterminate spinner arc
    const qreal spinnerSize = 22;
    const QRectF spinnerRect(card.center().x() - spinnerSize / 2,
                              card.bottom() - 44, spinnerSize, spinnerSize);
    QPen spinPen(t.accent);
    spinPen.setWidthF(2.4);
    spinPen.setCapStyle(Qt::RoundCap);
    p.setPen(spinPen);
    p.drawArc(spinnerRect, int(-m_spinAngle * 16), 100 * 16);
}

void SplashScreen::fadeOutAndClose() {
    m_spinTimer->stop();
    auto *anim = new QPropertyAnimation(this, "windowOpacity", this);
    anim->setDuration(280);
    anim->setStartValue(1.0);
    anim->setEndValue(0.0);
    connect(anim, &QPropertyAnimation::finished, this, &QWidget::close);
    connect(anim, &QPropertyAnimation::finished, this, &QObject::deleteLater);
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}
