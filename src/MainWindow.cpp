#include "MainWindow.h"
#include "ui/DeviceDiscoveryDialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>
#include <QCloseEvent>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QStatusBar>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QMessageBox>
#include <QPainter>
#include <QPropertyAnimation>
#include <QButtonGroup>
#include <QAbstractButton>

// ── Colour constants ──────────────────────────────────────────────────────────
static const QString C_BRAND       = "#8222E3";
static const QString C_BRAND_DARK  = "#6A1AB8";
static const QString C_SIDEBAR_BG  = "#120629";
static const QString C_SIDEBAR_SEL = "#8222E3";
static const QString C_SIDEBAR_HOV = "#1E0A40";
static const QString C_PAGE_BG     = "#F4F6FB";

// ── NavButton: custom sidebar item ────────────────────────────────────────────
class NavButton : public QAbstractButton {
public:
    NavButton(const QString &icon, const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_icon(icon), m_label(label)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(52);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor bg = isChecked()  ? QColor(C_BRAND_DARK)
                  : underMouse() ? QColor(C_SIDEBAR_HOV)
                                 : Qt::transparent;
        if (bg.alpha() > 0) {
            p.fillRect(rect(), bg);
        }
        if (isChecked()) {
            p.fillRect(0, 0, 4, height(), QColor(C_BRAND));
        }

        QColor textColor = isChecked() ? QColor("#FFFFFF")
                         : QColor("#B0A8C8");
        p.setPen(textColor);

        QFont iconFont = font();
        iconFont.setPointSize(15);
        p.setFont(iconFont);
        p.drawText(QRect(18, 0, 28, height()), Qt::AlignVCenter | Qt::AlignLeft, m_icon);

        QFont labelFont = font();
        labelFont.setPointSize(10);
        if (isChecked()) labelFont.setWeight(QFont::DemiBold);
        p.setFont(labelFont);
        p.drawText(QRect(52, 0, width() - 52, height()), Qt::AlignVCenter | Qt::AlignLeft, m_label);
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
private:
    QString m_icon, m_label;
};

// ── MainWindow ─────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_db = new Database(this);
    QString dbPath;
#ifdef Q_OS_WIN
    dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/biosync.db";
#else
    dbPath = QDir::homePath() + "/.biosync/biosync.db";
#endif
    m_db->open(dbPath);

    m_server = new ZkAdmsServer(this);
    m_pusher = new ApiPusher(m_db, this);

    setupUi();
    setupTray();
    startServer();
    reloadApiConfig();
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    setWindowTitle("BioSync");
    setWindowIcon(QIcon(":/assets/images/icon.png"));
    setMinimumSize(1120, 700);
    resize(1280, 780);

    // ── Sidebar ──────────────────────────────────────────────────────────────
    auto *sidebar = new QWidget();
    sidebar->setFixedWidth(200);
    sidebar->setStyleSheet(QString("background:%1;").arg(C_SIDEBAR_BG));

    // Logo area
    auto *logoArea = new QWidget();
    logoArea->setFixedHeight(72);
    logoArea->setStyleSheet(QString(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:1,"
        "stop:0 %1, stop:1 %2);").arg(C_BRAND, C_BRAND_DARK));

    auto *logoTitle = new QLabel("BioSync");
    logoTitle->setAlignment(Qt::AlignCenter);
    logoTitle->setStyleSheet("color:#FFFFFF; font-size:22px; font-weight:700; letter-spacing:1px;");
    auto *logoSub = new QLabel("Attendance Manager");
    logoSub->setAlignment(Qt::AlignCenter);
    logoSub->setStyleSheet("color:rgba(255,255,255,0.65); font-size:10px;");

    auto *logoLayout = new QVBoxLayout(logoArea);
    logoLayout->setContentsMargins(0, 10, 0, 8);
    logoLayout->setSpacing(2);
    logoLayout->addWidget(logoTitle);
    logoLayout->addWidget(logoSub);

    // Nav buttons
    m_navBtns[0] = new NavButton("󰌢", "Devices",    sidebar);
    m_navBtns[1] = new NavButton("󰃰", "Attendance", sidebar);
    m_navBtns[2] = new NavButton("󰍡", "Logs",       sidebar);
    m_navBtns[3] = new NavButton("󰒓", "Settings",   sidebar);

    // Use plain-text icons since we can't guarantee icon fonts
    m_navBtns[0] = new NavButton("⬡", "Devices",    sidebar);
    m_navBtns[1] = new NavButton("☑", "Attendance", sidebar);
    m_navBtns[2] = new NavButton("≡", "Logs",       sidebar);
    m_navBtns[3] = new NavButton("⚙", "Settings",   sidebar);

    m_navBtns[0]->setChecked(true);

    auto *navGroup = new QButtonGroup(this);
    for (int i = 0; i < 4; ++i) {
        navGroup->addButton(m_navBtns[i], i);
    }
    connect(navGroup, &QButtonGroup::idClicked, this, &MainWindow::onNavItemChanged);

    // Divider
    auto *navDivider = new QFrame();
    navDivider->setFrameShape(QFrame::HLine);
    navDivider->setStyleSheet("color: rgba(255,255,255,0.08);");

    // Version label at bottom of sidebar
    auto *versionLabel = new QLabel("v1.0.0");
    versionLabel->setAlignment(Qt::AlignCenter);
    versionLabel->setStyleSheet("color:rgba(255,255,255,0.25); font-size:10px; padding:8px 0;");

    auto *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    sideLayout->addWidget(logoArea);

    auto *navSpacer = new QWidget();
    navSpacer->setFixedHeight(12);
    sideLayout->addWidget(navSpacer);

    for (int i = 0; i < 4; ++i) {
        if (i == 3) sideLayout->addStretch();
        sideLayout->addWidget(m_navBtns[i]);
    }
    sideLayout->addWidget(versionLabel);

    // ── Pages ────────────────────────────────────────────────────────────────
    m_devicesPage    = new DevicesPage(m_db);
    m_attendancePage = new AttendancePage(m_db);
    m_logsPage       = new LogsPage(m_db);
    m_settingsPage   = new SettingsPage(m_db);

    m_stack = new QStackedWidget();
    m_stack->addWidget(m_devicesPage);
    m_stack->addWidget(m_attendancePage);
    m_stack->addWidget(m_logsPage);
    m_stack->addWidget(m_settingsPage);

    auto *contentArea = new QWidget();
    contentArea->setStyleSheet(QString("background:%1;").arg(C_PAGE_BG));
    auto *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_stack);

    // ── Root layout ──────────────────────────────────────────────────────────
    auto *root = new QWidget();
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(sidebar);
    rootLayout->addWidget(contentArea);
    setCentralWidget(root);

    // ── Status bar ───────────────────────────────────────────────────────────
    statusBar()->setStyleSheet(
        "QStatusBar { background:#FFFFFF; border-top:1px solid #E5E7EB; font-size:11px; color:#6B7280; }"
        "QStatusBar::item { border:none; }");

    m_statusServer  = new QLabel("● Server starting...");
    m_statusApi     = new QLabel("○ API unknown");
    m_statusPending = new QLabel("0 pending");

    auto *dot1 = new QLabel("|"); dot1->setStyleSheet("color:#D1D5DB;");
    auto *dot2 = new QLabel("|"); dot2->setStyleSheet("color:#D1D5DB;");

    statusBar()->addWidget(m_statusServer);
    statusBar()->addWidget(dot1);
    statusBar()->addWidget(m_statusApi);
    statusBar()->addWidget(dot2);
    statusBar()->addWidget(m_statusPending);

    // ── Signal connections ───────────────────────────────────────────────────
    connect(m_server, &ZkAdmsServer::attendanceReceived,     this, &MainWindow::onAttendanceReceived);
    connect(m_server, &ZkAdmsServer::deviceHeartbeat,        this, &MainWindow::onDeviceHeartbeat);
    connect(m_server, &ZkAdmsServer::unknownDeviceConnected, this, &MainWindow::onUnknownDeviceConnected);
    connect(m_server, &ZkAdmsServer::logMessage,             this, &MainWindow::onLogMessage);
    connect(m_server, &ZkAdmsServer::biometricTemplatesReceived,
            this, &MainWindow::onBiometricTemplatesReceived);

    connect(m_pusher, &ApiPusher::statusChanged,  this, &MainWindow::onApiStatusChanged);
    connect(m_pusher, &ApiPusher::recordsSynced,  this, &MainWindow::onApiRecordsSynced);
    connect(m_pusher, &ApiPusher::logMessage,     this, &MainWindow::onApiLogMessage);

    connect(m_devicesPage, &DevicesPage::devicesChanged, this, [this]() {
        syncKnownSerials();
        m_logsPage->refreshDeviceFilter();
    });
    connect(m_devicesPage,  &DevicesPage::scanNetworkRequested,   this, &MainWindow::onScanNetwork);
    connect(m_devicesPage,  &DevicesPage::activateDeviceRequested, this, &MainWindow::onActivateDevice);
    connect(m_devicesPage,  &DevicesPage::deactivateDeviceRequested, this, &MainWindow::onDeactivateDevice);
    connect(m_devicesPage,  &DevicesPage::reconnectDeviceRequested, this, &MainWindow::onReconnectDevice);
    connect(m_devicesPage,  &DevicesPage::restartDeviceRequested,  this, &MainWindow::onRestartDevice);
    connect(m_devicesPage,  &DevicesPage::backupTemplatesRequested,  this, &MainWindow::onBackupTemplates);
    connect(m_devicesPage,  &DevicesPage::restoreTemplatesRequested, this, &MainWindow::onRestoreTemplates);
    connect(m_settingsPage, &SettingsPage::settingsSaved,          this, &MainWindow::onSettingsSaved);
    connect(m_settingsPage, &SettingsPage::serverStartRequested,   this, &MainWindow::onServerStart);
    connect(m_settingsPage, &SettingsPage::serverStopRequested,    this, &MainWindow::onServerStop);
    connect(m_settingsPage, &SettingsPage::serverRestartRequested, this, &MainWindow::onServerRestart);
}

void MainWindow::setupTray() {
    m_trayIcon = new QSystemTrayIcon(QIcon(":/assets/images/icon.png"), this);
    m_trayIcon->setToolTip("BioSync — Attendance Manager");

    auto *trayMenu = new QMenu(this);
    trayMenu->addAction("Show BioSync", this, &QWidget::show);
    trayMenu->addSeparator();
    trayMenu->addAction("Quit", qApp, &QApplication::quit);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
}

void MainWindow::startServer() {
    syncKnownSerials();
    quint16 port = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    bool ok = m_server->start(port);
    updateServerStatus(ok, port);
    m_settingsPage->updateServerStatus(ok, port);
}

void MainWindow::reloadApiConfig() {
    QString apiUrl = m_db->getSetting("api_url");
    QString apiKey = m_db->getSetting("api_key");
    long long instId = m_db->getSetting("institution_id", "0").toLongLong();
    m_pusher->setConfig(apiUrl, apiKey, instId);
}

void MainWindow::syncKnownSerials() {
    QSet<QString> serials;
    for (const Device &d : m_db->getAllDevices())
        serials.insert(d.serialNumber);
    m_server->setKnownSerials(serials);
}

int MainWindow::autoRegisterDevice(const QString &serial, const QString &ip) {
    Device d;
    d.name         = QString("Device %1").arg(serial);
    d.serialNumber = serial;
    d.ipAddress    = ip;
    d.port         = 4370;
    d.isActive     = true;
    int id = m_db->addDevice(d);
    if (id > 0) {
        m_server->addKnownSerial(serial);
        m_devicesPage->refresh();
        m_logsPage->refreshDeviceFilter();
    }
    return id;
}

void MainWindow::updateServerStatus(bool running, quint16 port) {
    if (running) {
        m_statusServer->setText(QString("● Server on :%1").arg(port));
        m_statusServer->setStyleSheet("color:#10B981; font-weight:600;");
    } else {
        m_statusServer->setText("✕ Server stopped");
        m_statusServer->setStyleSheet("color:#EF4444; font-weight:600;");
    }
}

void MainWindow::onAttendanceReceived(const QList<AttendanceRecord> &records) {
    for (auto r : records) {
        Device d = m_db->getDeviceBySerial(r.deviceSerial);
        if (d.id && !d.isActive) continue;  // skip deactivated devices
        if (!d.name.isEmpty()) r.deviceName = d.name;
        int id = m_db->addAttendanceRecord(r);
        if (id > 0) { r.id = id; m_attendancePage->prependRecord(r); }
    }
    m_pusher->pushNow();
    m_statusPending->setText(QString("%1 pending").arg(m_db->getPendingCount()));
}

void MainWindow::onDeviceHeartbeat(const QString &serial, const QString &ip) {
    bool wasOnline = m_deviceOnlineMap.value(serial, false);
    m_deviceOnlineMap[serial] = true;
    m_devicesPage->markDeviceOnline(serial, true);
    m_devicesPage->setDeviceSocketIp(serial, ip);
    if (!wasOnline)
        onLogMessage(serial, QString("Device came online from %1").arg(ip));
    m_db->appendLog(serial, QString("Heartbeat from %1").arg(ip));
}

void MainWindow::onUnknownDeviceConnected(const QString &serial, const QString &ip) {
    onLogMessage(serial, QString("New device detected SN=%1 from %2 — auto-registering.").arg(serial, ip));
    int id = autoRegisterDevice(serial, ip);
    if (id > 0) {
        m_trayIcon->showMessage("BioSync — New Device Detected",
            QString("Device %1 (%2) registered automatically.").arg(serial, ip),
            QSystemTrayIcon::Information, 4000);
    }
}

void MainWindow::onScanNetwork() {
    QSet<QString> existing;
    for (const Device &d : m_db->getAllDevices())
        existing.insert(d.serialNumber);

    DeviceDiscoveryDialog dlg(existing, this);
    if (dlg.exec() == QDialog::Accepted) {
        int added = 0;
        for (const Device &d : dlg.selectedDevices()) {
            if (m_db->addDevice(d) > 0) { m_server->addKnownSerial(d.serialNumber); added++; }
        }
        if (added > 0) {
            m_devicesPage->refresh();
            m_logsPage->refreshDeviceFilter();
            QMessageBox::information(this, "Devices Added",
                QString("%1 device(s) added successfully.").arg(added));
        }
    }
}

void MainWindow::onLogMessage(const QString &deviceSerial, const QString &message) {
    m_logsPage->appendLog(deviceSerial, message);
    m_db->appendLog(deviceSerial, message);
}

void MainWindow::onApiStatusChanged(bool online, int pendingCount) {
    if (online) {
        m_statusApi->setText("● API connected");
        m_statusApi->setStyleSheet("color:#10B981; font-weight:600;");
    } else {
        m_statusApi->setText("○ API offline");
        m_statusApi->setStyleSheet("color:#F59E0B; font-weight:600;");
    }
    m_statusPending->setText(QString("%1 pending").arg(pendingCount));
}

void MainWindow::onApiRecordsSynced(int) {
    m_statusPending->setText(QString("%1 pending").arg(m_db->getPendingCount()));
    m_attendancePage->refresh();
}

void MainWindow::onApiLogMessage(const QString &msg) { onLogMessage({}, msg); }

void MainWindow::onSettingsSaved() { reloadApiConfig(); }

void MainWindow::onActivateDevice(int deviceId) {
    Device d = m_db->getDevice(deviceId);
    if (!d.id) return;
    d.isActive = true;
    if (m_db->updateDevice(d)) {
        onLogMessage(d.serialNumber, QString("Device %1 activated.").arg(d.serialNumber));
        m_devicesPage->refresh();
    }
}

void MainWindow::onDeactivateDevice(int deviceId) {
    Device d = m_db->getDevice(deviceId);
    if (!d.id) return;
    d.isActive = false;
    if (m_db->updateDevice(d)) {
        m_deviceOnlineMap[d.serialNumber] = false;
        m_devicesPage->markDeviceOnline(d.serialNumber, false);
        onLogMessage(d.serialNumber, QString("Device %1 deactivated — attendance data will be ignored.").arg(d.serialNumber));
        m_devicesPage->refresh();
    }
}

void MainWindow::onReconnectDevice(const QString &serial) {
    // Mark device offline so it re-registers on next heartbeat
    m_deviceOnlineMap[serial] = false;
    m_devicesPage->markDeviceOnline(serial, false);
    onLogMessage(serial, QString("Reconnect requested for SN=%1 — marked offline, waiting for re-connect.").arg(serial));
}

void MainWindow::onRestartDevice(const QString &serial) {
    // Queue a REBOOT command delivered on the device's next heartbeat poll
    m_server->queueCommand(serial, "REBOOT");
    m_deviceOnlineMap[serial] = false;
    m_devicesPage->markDeviceOnline(serial, false);
    onLogMessage(serial, QString("Restart command queued for SN=%1.").arg(serial));
    m_trayIcon->showMessage("BioSync — Device Restart",
        QString("Restart command sent to device %1.").arg(serial),
        QSystemTrayIcon::Information, 3000);
}

void MainWindow::onBackupTemplates(const QString &serial) {
    // Close any existing dialog for this serial
    if (m_backupDialogs.contains(serial)) {
        m_backupDialogs[serial]->close();
        m_backupDialogs.remove(serial);
    }

    Device d = m_db->getDeviceBySerial(serial);
    QString apiUrl = m_db->getSetting("api_url");
    QString apiKey = m_db->getSetting("api_key");
    long long instId = m_db->getSetting("institution_id", "0").toLongLong();

    auto *dlg = new BiometricBackupDialog(serial, d.name, apiUrl, apiKey, instId, this);
    m_backupDialogs[serial] = dlg;

    connect(dlg, &BiometricBackupDialog::backupFinished,
            this, [this](const QString &sn, int total, bool ok) {
        onLogMessage(sn, QString("Biometric backup %1 — %2 template(s) for SN=%3.")
                     .arg(ok ? "complete" : "failed").arg(total).arg(sn));
    });
    // WA_DeleteOnClose handles deletion; remove from tracking map when dialog closes
    connect(dlg, &QObject::destroyed, this, [this, serial]() {
        m_backupDialogs.remove(serial);
    });

    dlg->show();

    // Queue the ADMS DATA QUERY — templates will arrive on device's next poll
    m_server->queueBiometricQuery(serial);
    onLogMessage(serial, QString("Biometric backup initiated for SN=%1 — "
                                 "waiting for device to upload templates.").arg(serial));
}

void MainWindow::onBiometricTemplatesReceived(const QString &serial,
                                               const QList<BiometricTemplate> &templates)
{
    onLogMessage(serial, QString("Received %1 biometric template(s) from SN=%2.")
                 .arg(templates.size()).arg(serial));

    if (m_backupDialogs.contains(serial)) {
        // Dialog is open — let it handle the upload with progress display
        m_backupDialogs[serial]->startUpload(templates);
    } else {
        // No dialog (e.g. queried while app was backgrounded) — push silently
        Device d = m_db->getDeviceBySerial(serial);
        m_pusher->pushBiometricTemplates(serial, d.name, templates);
        m_trayIcon->showMessage("BioSync — Backup Complete",
            QString("Backed up %1 biometric template(s) from device %2.")
                .arg(templates.size()).arg(serial),
            QSystemTrayIcon::Information, 4000);
    }
}

void MainWindow::onRestoreTemplates(const QString &serial) {
    Device d = m_db->getDeviceBySerial(serial);

    auto msgStyle = QString(
        "QMessageBox { background:#FFFFFF; }"
        "QMessageBox QPushButton { background:#8222E3; color:#FFFFFF; border-radius:8px;"
        "  padding:6px 20px; font-weight:600; }"
        "QMessageBox QPushButton:hover { background:#6A1AB8; }");

    QMessageBox mb(this);
    mb.setWindowTitle("Restore Biometric Templates");
    mb.setText(QString("Restore backed-up templates to device %1 (%2)?").arg(d.name, serial));
    mb.setInformativeText(
        "Backed-up templates will be fetched from the server and pushed to the device "
        "on its next heartbeat poll via ADMS DATA UPDATE command.");
    mb.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    mb.setIcon(QMessageBox::Question);
    mb.setStyleSheet(msgStyle);
    if (mb.exec() != QMessageBox::Ok) return;

    onLogMessage(serial, QString("Fetching templates from server for SN=%1…").arg(serial));

    m_pusher->fetchBiometricTemplates(serial, [this, serial](QList<BiometricTemplate> templates) {
        if (templates.isEmpty()) {
            QMessageBox::warning(this, "Restore Templates",
                QString("No backed-up templates found on the server for device %1.").arg(serial));
            onLogMessage(serial, QString("Restore cancelled — no templates on server for SN=%1.").arg(serial));
            return;
        }
        m_server->queueBiometricRestore(serial, templates);
        m_trayIcon->showMessage("BioSync — Restore Queued",
            QString("%1 template(s) will be pushed to device %2 on next poll.")
                .arg(templates.size()).arg(serial),
            QSystemTrayIcon::Information, 4000);
    });
}

void MainWindow::onServerStart() {
    if (m_server->isRunning()) return;
    startServer();
}

void MainWindow::onServerStop() {
    m_server->stop();
    updateServerStatus(false, 0);
    m_settingsPage->updateServerStatus(false, 0);
    // Mark all devices offline
    for (const QString &serial : m_deviceOnlineMap.keys()) {
        m_deviceOnlineMap[serial] = false;
        m_devicesPage->markDeviceOnline(serial, false);
    }
}

void MainWindow::onServerRestart() {
    onServerStop();
    startServer();
}

void MainWindow::onNavItemChanged(int row) {
    m_stack->setCurrentIndex(row);
    for (int i = 0; i < 4; ++i) m_navBtns[i]->setChecked(i == row);
    if (row == 1) m_attendancePage->refresh();
    else if (row == 2) m_logsPage->refreshDeviceFilter();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) { show(); raise(); activateWindow(); }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_trayIcon->isVisible()) {
        hide();
        m_trayIcon->showMessage("BioSync", "Running in background. Double-click tray icon to restore.",
                                QSystemTrayIcon::Information, 2000);
        event->ignore();
    } else {
        event->accept();
    }
}
