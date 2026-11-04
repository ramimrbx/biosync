#pragma once
#include <QWidget>
#include <QTimer>

// A frameless, always-on-top splash shown briefly on every GUI launch (including a silent
// autostart run) while MainWindow does its real startup work (DB open, ADMS server bind).
// Call fadeOutAndClose() when ready to dismiss it; it animates its opacity down, then closes
// and deletes itself.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(QWidget *parent = nullptr);

    void fadeOutAndClose();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_spinAngle = 0.0;
    QTimer *m_spinTimer;
};
