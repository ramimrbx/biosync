#include "BiometricBackupDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>

// ── Helpers ───────────────────────────────────────────────────────────────────
static QLabel *makeValueLabel(const QString &text = "—") {
    auto *lbl = new QLabel(text);
    lbl->setStyleSheet("font-size:13px; color:#111827; font-weight:600;");
    return lbl;
}
static QLabel *makeCaptionLabel(const QString &text) {
    auto *lbl = new QLabel(text);
    lbl->setStyleSheet("font-size:12px; color:#6B7280;");
    lbl->setFixedWidth(180);
    return lbl;
}
static QFrame *makeDivider() {
    auto *f = new QFrame();
    f->setFrameShape(QFrame::HLine);
    f->setStyleSheet("color:#F3F4F6;");
    return f;
}

// ── Constructor ───────────────────────────────────────────────────────────────
BiometricBackupDialog::BiometricBackupDialog(const QString &serial,
                                             const QString &deviceName,
                                             const QString &apiUrl,
                                             const QString &apiKey,
                                             long long      institutionId,
                                             QWidget       *parent)
    : QDialog(parent),
      m_serial(serial), m_deviceName(deviceName),
      m_apiUrl(apiUrl), m_apiKey(apiKey), m_institutionId(institutionId)
{
    setAttribute(Qt::WA_DeleteOnClose);   // auto-delete when window is closed

    m_startedAt = QDateTime::currentDateTime();
    m_nam = new QNetworkAccessManager(this);

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &BiometricBackupDialog::onStatsTimer);

    buildUi();
    setStatus("Waiting for device to respond…", "#F59E0B");
    m_progressBar->setMaximum(0);   // indeterminate pulse while waiting for device
    m_statsTimer->start();
}

void BiometricBackupDialog::buildUi() {
    setWindowTitle("Biometric Template Backup");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setFixedWidth(520);
    setStyleSheet("background:#FFFFFF;");

    // ── Header banner ─────────────────────────────────────────────────────────
    auto *banner = new QWidget();
    banner->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8222E3,stop:1 #A855F7);"
        "border-radius:10px;");
    banner->setFixedHeight(72);
    auto *bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(20, 0, 20, 0);

    auto *icon = new QLabel("🔒");
    icon->setStyleSheet("font-size:26px;");

    auto *titleCol = new QVBoxLayout();
    auto *bannerTitle = new QLabel("Biometric Template Backup");
    bannerTitle->setStyleSheet("color:#FFFFFF; font-size:14px; font-weight:700;");

    m_lblDeviceInfo = new QLabel(
        QString("%1  ·  SN: %2").arg(m_deviceName.isEmpty() ? "Unknown" : m_deviceName, m_serial));
    m_lblDeviceInfo->setStyleSheet("color:rgba(255,255,255,0.75); font-size:11px;");
    titleCol->setSpacing(2);
    titleCol->addWidget(bannerTitle);
    titleCol->addWidget(m_lblDeviceInfo);

    bannerLayout->addWidget(icon);
    bannerLayout->addSpacing(8);
    bannerLayout->addLayout(titleCol);
    bannerLayout->addStretch();

    // ── Status label ──────────────────────────────────────────────────────────
    m_lblStatus = new QLabel("Initialising…");
    m_lblStatus->setStyleSheet("font-size:12px; color:#374151;");
    m_lblStatus->setAlignment(Qt::AlignCenter);

    // ── Progress bar ──────────────────────────────────────────────────────────
    m_progressBar = new QProgressBar();
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(10);
    m_progressBar->setStyleSheet(
        "QProgressBar {"
        "  border:none; border-radius:5px; background:#F3F4F6;"
        "}"
        "QProgressBar::chunk {"
        "  border-radius:5px;"
        "  background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8222E3,stop:1 #A855F7);"
        "}");

    // ── Stats grid ────────────────────────────────────────────────────────────
    auto *statsWidget = new QWidget();
    statsWidget->setStyleSheet(
        "background:#F9FAFB; border-radius:10px; border:1.5px solid #E5E7EB;");
    auto *grid = new QGridLayout(statsWidget);
    grid->setContentsMargins(20, 16, 20, 16);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);

    m_lblTotal     = makeValueLabel("—");
    m_lblUploaded  = makeValueLabel("0");
    m_lblSpeed     = makeValueLabel("—");
    m_lblStartedAt = makeValueLabel(m_startedAt.toString("hh:mm:ss"));
    m_lblElapsed   = makeValueLabel("00:00");
    m_lblEta       = makeValueLabel("—");

    int row = 0;
    grid->addWidget(makeCaptionLabel("Total Templates"),      row, 0);
    grid->addWidget(m_lblTotal,                               row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Uploaded"),             row, 0);
    grid->addWidget(m_lblUploaded,                            row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Upload Speed"),         row, 0);
    grid->addWidget(m_lblSpeed,                               row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Started At"),           row, 0);
    grid->addWidget(m_lblStartedAt,                           row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Elapsed"),              row, 0);
    grid->addWidget(m_lblElapsed,                             row, 1); ++row;
    grid->addWidget(makeDivider(),                            row, 0, 1, 2); ++row;
    grid->addWidget(makeCaptionLabel("Estimated Remaining"),  row, 0);
    grid->addWidget(m_lblEta,                                 row, 1);

    // ── Close button ──────────────────────────────────────────────────────────
    m_btnClose = new QPushButton("Close");
    m_btnClose->setFixedHeight(40);
    m_btnClose->setStyleSheet(
        "QPushButton { background:#8222E3; color:#FFFFFF; border:none;"
        "  padding:0 28px; border-radius:8px; font-size:13px; font-weight:600; }"
        "QPushButton:hover   { background:#6A1AB8; }"
        "QPushButton:pressed { background:#5a14a0; }"
        "QPushButton:disabled { background:#D1D5DB; color:#9CA3AF; }");
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

// ── Public API ────────────────────────────────────────────────────────────────

void BiometricBackupDialog::startUpload(const QList<BiometricTemplate> &templates) {
    if (m_uploading || m_finished) return;

    m_templates = templates;
    m_total     = templates.size();
    m_uploaded  = 0;
    m_uploading = true;
    m_uploadStartedAt = QDateTime::currentDateTime();

    m_lblTotal->setText(QString::number(m_total));
    m_progressBar->setMaximum(m_total > 0 ? m_total : 1);
    m_progressBar->setValue(0);

    if (m_total == 0) {
        setStatus("No templates found on device.", "#EF4444");
        finishUpload(false);
        return;
    }

    setStatus(QString("Received %1 template(s) from device — uploading to server…").arg(m_total),
              "#8222E3");
    doUpload();
}

// ── Upload — single request for all templates (no artificial limit) ───────────

void BiometricBackupDialog::doUpload() {
    if (!m_uploading || m_finished) return;

    QJsonArray arr;
    for (const auto &t : std::as_const(m_templates)) {
        QJsonObject obj;
        obj["userPin"]      = t.userPin;
        obj["fingerIndex"]  = t.fingerIndex;
        obj["templateData"] = t.templateData;
        arr.append(obj);
    }

    QJsonObject payload;
    payload["deviceSerial"]  = m_serial;
    payload["deviceName"]    = m_deviceName;
    payload["institutionId"] = static_cast<qint64>(m_institutionId);
    payload["templates"]     = arr;

    QNetworkRequest req(QUrl(m_apiUrl + "/api/v1/biosync/templates/backup"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("X-BioSync-Key", m_apiKey.toUtf8());
    req.setTransferTimeout(0);   // no timeout — payload can be very large

    QNetworkReply *reply = m_nam->post(req,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    // Track upload progress via bytesWritten
    connect(reply, &QNetworkReply::uploadProgress,
            this,  [this](qint64 sent, qint64 total) {
        if (total <= 0 || m_total <= 0) return;
        // Map byte-level upload progress to template count
        int approx = static_cast<int>(
            static_cast<double>(sent) / total * m_total);
        m_uploaded = qMin(approx, m_total - 1);   // stay below total until confirmed
        m_progressBar->setValue(m_uploaded);
        m_lblUploaded->setText(QString("%1 / %2").arg(m_uploaded).arg(m_total));
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        bool ok = (reply->error() == QNetworkReply::NoError && httpStatus == 200);
        reply->deleteLater();

        m_uploaded = ok ? m_total : m_uploaded;
        m_progressBar->setValue(m_uploaded);
        m_lblUploaded->setText(QString("%1 / %2").arg(m_uploaded).arg(m_total));

        finishUpload(ok);
    });
}

void BiometricBackupDialog::finishUpload(bool success) {
    m_uploading = false;
    m_finished  = true;
    m_statsTimer->stop();

    // Clear template data from memory now that upload is done
    m_templates.clear();
    m_templates.squeeze();

    qint64 elapsed = m_startedAt.secsTo(QDateTime::currentDateTime());
    m_lblElapsed->setText(formatDuration(elapsed));
    m_lblEta->setText("—");

    if (success) {
        m_progressBar->setMaximum(m_total > 0 ? m_total : 1);
        m_progressBar->setValue(m_total > 0 ? m_total : 1);
        setStatus(QString("✓  Backup complete — %1 template(s) uploaded successfully.").arg(m_total),
                  "#10B981");
        m_progressBar->setStyleSheet(
            "QProgressBar { border:none; border-radius:5px; background:#F3F4F6; }"
            "QProgressBar::chunk { border-radius:5px; background:#10B981; }");
    } else {
        setStatus("✕  Upload failed. Check API connection and try again.", "#EF4444");
        m_progressBar->setStyleSheet(
            "QProgressBar { border:none; border-radius:5px; background:#F3F4F6; }"
            "QProgressBar::chunk { border-radius:5px; background:#EF4444; }");
    }

    m_btnClose->setEnabled(true);
    emit backupFinished(m_serial, m_uploaded, success);
}

// ── Stats timer (updates elapsed / speed / ETA every second) ─────────────────

void BiometricBackupDialog::onStatsTimer() {
    qint64 elapsed = m_startedAt.secsTo(QDateTime::currentDateTime());
    m_lblElapsed->setText(formatDuration(elapsed));

    if (!m_uploading || m_uploadStartedAt.isNull()) return;

    qint64 uploadElapsed = m_uploadStartedAt.secsTo(QDateTime::currentDateTime());
    if (uploadElapsed > 0 && m_uploaded > 0) {
        double speed = static_cast<double>(m_uploaded) / uploadElapsed;
        m_lblSpeed->setText(QString("%1 templates/sec").arg(speed, 0, 'f', 1));
        int remaining = m_total - m_uploaded;
        m_lblEta->setText(formatDuration(static_cast<qint64>(remaining / speed)));
    }
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
