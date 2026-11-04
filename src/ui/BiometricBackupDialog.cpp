#include "BiometricBackupDialog.h"
#include "ui/Theme.h"
#include "../db/Database.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>

// ── Helpers ───────────────────────────────────────────────────────────────────
static QLabel *makeValueLabel(const QString &text = "—") {
    auto *lbl = new QLabel(text);
    lbl->setStyleSheet(QString("font-size:13px; color:%1; font-weight:600;")
                            .arg(Theme::current().textPrimary.name()));
    return lbl;
}
static QLabel *makeCaptionLabel(const QString &text) {
    auto *lbl = new QLabel(text);
    lbl->setStyleSheet(QString("font-size:12px; color:%1;").arg(Theme::current().textSecondary.name()));
    lbl->setFixedWidth(180);
    return lbl;
}
static QFrame *makeDivider() {
    auto *f = new QFrame();
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet(QString("color:%1;").arg(Theme::current().border.name()));
    return f;
}

// ── Constructor ───────────────────────────────────────────────────────────────
BiometricBackupDialog::BiometricBackupDialog(const QString &serial,
                                             const QString &deviceName,
                                             Database      *db,
                                             int            commandId,
                                             QWidget       *parent)
    : QDialog(parent),
      m_serial(serial), m_deviceName(deviceName), m_db(db), m_commandId(commandId)
{
    setAttribute(Qt::WA_DeleteOnClose);   // auto-delete when window is closed

    m_startedAt = QDateTime::currentDateTime();

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &BiometricBackupDialog::onPoll);

    buildUi();
    setStatus("Waiting for the device to check in…", Theme::current().warning.name());
    m_progressBar->setMaximum(0);   // indeterminate pulse while waiting
    m_pollTimer->start();
}

void BiometricBackupDialog::buildUi() {
    const ThemePalette &t = Theme::current();
    setWindowTitle("Biometric Template Backup");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedWidth(520);
    setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));

    // ── Header ───────────────────────────────────────────────────────────────
    // Mirrors ritems' Modal.tsx "default" header: a soft accent wash with an accent-colored
    // title, not a solid brand-color banner.
    auto *banner = new QWidget();
    banner->setAttribute(Qt::WA_StyledBackground, true);
    banner->setStyleSheet(QString("background:%1; border-radius:16px;").arg(t.accentSoft()));
    banner->setFixedHeight(72);
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(20, 0, 20, 0);

    auto *icon = new QLabel("🔒");
    icon->setStyleSheet("font-size:26px;");

    auto *titleCol = new QVBoxLayout();
    auto *bannerTitle = new QLabel("Biometric Template Backup");
    bannerTitle->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;").arg(t.accent.name()));

    m_lblDeviceInfo = new QLabel(
        QString("%1  ·  SN: %2").arg(m_deviceName.isEmpty() ? "Unknown" : m_deviceName, m_serial));
    m_lblDeviceInfo->setStyleSheet(QString("color:%1; font-size:11px;").arg(t.textSecondary.name()));
    titleCol->setSpacing(2);
    titleCol->addWidget(bannerTitle);
    titleCol->addWidget(m_lblDeviceInfo);

    bannerLayout->addWidget(icon);
    bannerLayout->addSpacing(8);
    bannerLayout->addLayout(titleCol);
    bannerLayout->addStretch();

    // ── Status label ──────────────────────────────────────────────────────────
    m_lblStatus = new QLabel("Initialising…");
    m_lblStatus->setStyleSheet(QString("font-size:12px; color:%1;").arg(t.textSecondary.name()));
    m_lblStatus->setAlignment(Qt::AlignCenter);

    // ── Progress bar ──────────────────────────────────────────────────────────
    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(10);
    m_progressBar->setStyleSheet(QString(
        "QProgressBar {"
        "  border:none; border-radius:5px; background:%1;"
        "}"
        "QProgressBar::chunk {"
        "  border-radius:5px;"
        "  background:%2;"
        "}").arg(t.border.name(), t.accent.name()));

    // ── Stats grid ────────────────────────────────────────────────────────────
    auto *statsWidget = new QWidget();
    statsWidget->setAttribute(Qt::WA_StyledBackground, true);
    statsWidget->setStyleSheet(QString(
        "background:%1; border-radius:12px; border:1.5px solid %2;")
        .arg(t.headerBg.name(), t.border.name()));
    auto *grid = new QGridLayout(statsWidget);
    grid->setContentsMargins(20, 16, 20, 16);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);

    m_lblTotal     = makeValueLabel("—");
    m_lblUploaded  = makeValueLabel("0");
    m_lblStartedAt = makeValueLabel(m_startedAt.toString("hh:mm:ss"));
    m_lblElapsed   = makeValueLabel("00:00");

    int row = 0;
    grid->addWidget(makeCaptionLabel("Total Templates"),      row, 0);
    grid->addWidget(m_lblTotal,                               row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Uploaded"),             row, 0);
    grid->addWidget(m_lblUploaded,                            row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Started At"),           row, 0);
    grid->addWidget(m_lblStartedAt,                           row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Elapsed"),              row, 0);
    grid->addWidget(m_lblElapsed,                             row, 1);

    // ── Close button ──────────────────────────────────────────────────────────
    m_btnClose = new QPushButton("Close");
    m_btnClose->setFixedHeight(40);
    m_btnClose->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:none;"
        "  padding:0 28px; border-radius:12px; font-size:13px; font-weight:600; }"
        "QPushButton:hover   { background:%3; }"
        "QPushButton:pressed { background:%4; }"
        "QPushButton:disabled { background:%5; color:%6; }")
        .arg(t.accent.name(), t.onAccent.name(), t.accentHover.name(), t.accentPressed.name(),
             t.border.name(), t.textDisabled.name()));
    m_btnClose->setEnabled(false);
    m_btnClose->setCursor(Qt::PointingHandCursor);
    connect(m_btnClose, &QPushButton::clicked, this, &QDialog::accept);

    // ── Assemble ──────────────────────────────────────────────────────────────
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(14);
    root->addWidget(banner);
    root->addWidget(m_lblStatus);
    root->addWidget(m_progressBar);
    root->addWidget(statsWidget);
    root->addWidget(m_btnClose, 0, Qt::AlignRight);
}

// ── Progress (polls the queued command in the shared DB) ──────────────────────

void BiometricBackupDialog::onPoll() {
    m_lblElapsed->setText(formatDuration(m_startedAt.secsTo(QDateTime::currentDateTime())));
    if (m_finished) return;

    const DeviceCommand cmd = m_db->getCommand(m_commandId);
    if (!cmd.id) { finish(false, "The backup request was lost."); return; }
    if (cmd.status == m_lastStatus && cmd.status != "sent") return;
    m_lastStatus = cmd.status;

    if (cmd.status == "pending") {
        setStatus("Waiting for the device to check in…", Theme::current().warning.name());
    } else if (cmd.status == "sent") {
        setStatus(cmd.result == "ack"
                      ? "Device accepted the request — waiting for its templates…"
                      : "Request delivered to the device — waiting for its templates…",
                  Theme::current().warning.name());
    } else if (cmd.status == "uploading") {
        m_total = cmd.result.toInt();
        m_lblTotal->setText(QString::number(m_total));
        m_lblUploaded->setText(QString("0 / %1").arg(m_total));
        setStatus(QString("Received %1 template(s) from the device — uploading to the server…").arg(m_total),
                  Theme::current().accent.name());
    } else if (cmd.status == "done") {
        m_total = cmd.result.toInt();
        finish(true, {});
    } else if (cmd.status == "failed") {
        finish(false, cmd.result);
    }
}

void BiometricBackupDialog::finish(bool success, const QString &message) {
    m_finished = true;
    m_pollTimer->stop();
    m_lblElapsed->setText(formatDuration(m_startedAt.secsTo(QDateTime::currentDateTime())));
    m_lblTotal->setText(QString::number(m_total));

    const ThemePalette &t = Theme::current();
    m_progressBar->setMaximum(1);
    m_progressBar->setValue(1);
    if (success) {
        m_lblUploaded->setText(QString("%1 / %1").arg(m_total));
        setStatus(m_total > 0
                      ? QString("✓  Backup complete — %1 template(s) uploaded successfully.").arg(m_total)
                      : QString("✓  Done — the device has no templates to back up."),
                  t.success.name());
        m_progressBar->setStyleSheet(QString(
            "QProgressBar { border:none; border-radius:5px; background:%1; }"
            "QProgressBar::chunk { border-radius:5px; background:%2; }").arg(t.border.name(), t.success.name()));
    } else {
        setStatus("✕  Backup failed. " + (message.isEmpty() ? "Check the API connection and try again." : message),
                  t.danger.name());
        m_progressBar->setStyleSheet(QString(
            "QProgressBar { border:none; border-radius:5px; background:%1; }"
            "QProgressBar::chunk { border-radius:5px; background:%2; }").arg(t.border.name(), t.danger.name()));
    }

    m_btnClose->setEnabled(true);
    emit backupFinished(m_serial, success ? m_total : 0, success);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void BiometricBackupDialog::setStatus(const QString &text, const QString &color) {
    m_lblStatus->setText(text);
    m_lblStatus->setStyleSheet(QString("font-size:12px; color:%1; font-weight:500;").arg(color));
}

QString BiometricBackupDialog::formatDuration(qint64 seconds) const {
    if (seconds < 0) seconds = 0;
    qint64 h = seconds / 3600;
    qint64 m = (seconds % 3600) / 60;
    qint64 s = seconds % 60;
    if (h > 0)
        return QString("%1:%2:%3")
            .arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2")
        .arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
