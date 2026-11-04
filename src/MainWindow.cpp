#include "MainWindow.h"
#include "AppPaths.h"
#include "Version.h"
#include "ui/DeviceDiscoveryDialog.h"
#include "ui/Theme.h"
#include "ui/Icons.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDialog>
#include <QGridLayout>
#include <QDesktopServices>
#include <QProgressDialog>
#include <QSaveFile>
#include <QTimer>
#include <QDateTime>
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
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QButtonGroup>
#include <QAbstractButton>

// ── NavButton: custom sidebar item ────────────────────────────────────────────
// Mirrors ritems/client/web's Sidebar NavItem: a rounded pill (mx-2, rounded-lg) that gets an
// even hover/active background (bg-gray-100 / slate-800), with the accent color reserved for the
// active row's icon+label and a short rounded bar pinned to the sidebar's left edge — rather than
// the old Fluent-style full-bleed accent fill.
class NavButton : public QAbstractButton {
public:
    NavButton(Icons::Glyph glyph, const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_glyph(glyph), m_label(label)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setFixedHeight(44);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        const ThemePalette &t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const bool active = isChecked();
        const bool hovered = underMouse();

        if (active) {
            const int barH = 24;
            p.fillRect(QRect(0, (height() - barH) / 2, 3, barH), t.accent);
        }

        const QRect pill = rect().adjusted(8, 3, -8, -3);
        if (active || hovered) {
            QPainterPath path;
            path.addRoundedRect(pill, 8, 8);
            p.fillPath(path, t.sidebarHover);
        }

        const QColor fg = active ? t.accent : hovered ? t.sidebarText : t.sidebarTextMuted;
        const QPixmap icon = Icons::pixmap(m_glyph, fg, 18, devicePixelRatioF());
        p.drawPixmap(QRect(pill.left() + 12, (height() - 18) / 2, 18, 18), icon);

        p.setPen(active ? t.accent : t.sidebarText);
        QFont labelFont = font();
        labelFont.setPointSize(10);
        if (active) labelFont.setWeight(QFont::DemiBold);
        p.setFont(labelFont);
        p.drawText(QRect(pill.left() + 44, 0, pill.width() - 44, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, m_label);
    }
    void enterEvent(QEnterEvent *e) override { update(); QAbstractButton::enterEvent(e); }
    void leaveEvent(QEvent *e)       override { update(); QAbstractButton::leaveEvent(e); }
private:
    Icons::Glyph m_glyph;
    QString m_label;
};

// ── MainWindow ─────────────────────────────────────────────────────────────────
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    m_db = new Database(this);
    // Shared machine-wide DB so the headless boot service reads the config set here in the GUI.
    m_db->open(AppPaths::dbPath());

    m_engine = new Engine(m_db, Engine::Role::Gui, this);
    m_pusher = new ApiPusher(m_db, this);
    m_pusher->setAutoPush(false);   // punches are pushed only by whoever runs the engine

    setupUi();
    setupTray();
    reloadApiConfig();
    connect(&Theme::instance(), &Theme::changed, this, &MainWindow::applyTheme);

    m_probeNam = new QNetworkAccessManager(this);

    m_superviseTimer = new QTimer(this);
    m_superviseTimer->setInterval(3000);
    connect(m_superviseTimer, &QTimer::timeout, this, &MainWindow::supervise);
    m_superviseTimer->start();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::refreshFromDb);
    m_refreshTimer->start();

    supervise();
    refreshFromDb();

    // Auto-check for a newer version shortly after launch (silent when already up to date),
    // then once a day while the app stays open.
    QTimer::singleShot(4000, this, [this]() { checkForUpdates(/*interactive=*/false); });
    auto *updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, [this]() { checkForUpdates(false); });
    updateTimer->start(24 * 60 * 60 * 1000);
}

MainWindow::~MainWindow() {}

void MainWindow::setupUi() {
    setWindowTitle("BioSync");
    setWindowIcon(QIcon(":/assets/images/icon.ico"));
    setMinimumSize(1120, 700);
    resize(1280, 780);

    // ── Sidebar ──────────────────────────────────────────────────────────────
    // Layout mirrors ritems/client/web's Sidebar.tsx: a fixed-width rail with a bordered logo
    // lockup at top (icon mark + wordmark, "Sync" in the accent color like ritems' "EMS"), nav
    // items below, and a bordered bottom bar (there: collapse toggle; here: version/About).
    m_sidebar = new QWidget();
    m_sidebar->setAttribute(Qt::WA_StyledBackground, true);
    m_sidebar->setFixedWidth(240);

    // Logo area
    m_logoArea = new QWidget();
    m_logoArea->setAttribute(Qt::WA_StyledBackground, true);
    m_logoArea->setFixedHeight(56);

    m_logoMark = new QLabel("B");
    m_logoMark->setAttribute(Qt::WA_StyledBackground, true);
    m_logoMark->setFixedSize(32, 32);
    m_logoMark->setAlignment(Qt::AlignCenter);

    m_logoTitle = new QLabel();
    m_logoTitle->setTextFormat(Qt::RichText);

    auto *logoLayout = new QHBoxLayout(m_logoArea);
    logoLayout->setContentsMargins(20, 0, 20, 0);
    logoLayout->setSpacing(10);
    logoLayout->addWidget(m_logoMark);
    logoLayout->addWidget(m_logoTitle);
    logoLayout->addStretch();

    // Nav buttons
    m_navBtns[0] = new NavButton(Icons::Glyph::Devices,    "Devices",    m_sidebar);
    m_navBtns[1] = new NavButton(Icons::Glyph::Attendance, "Attendance", m_sidebar);
    m_navBtns[2] = new NavButton(Icons::Glyph::Logs,       "Logs",       m_sidebar);
    m_navBtns[3] = new NavButton(Icons::Glyph::Settings,   "Settings",   m_sidebar);

    m_navBtns[0]->setChecked(true);

    auto *navGroup = new QButtonGroup(this);
    for (int i = 0; i < 4; ++i) {
        navGroup->addButton(m_navBtns[i], i);
    }
    connect(navGroup, &QButtonGroup::idClicked, this, &MainWindow::onNavItemChanged);

    // Version button at bottom of sidebar — real build version, opens the About panel. Styled
    // like ritems' bottom-of-sidebar "Collapse Sidebar" bar: fixed height, top border, muted
    // text that picks up the accent color on hover.
    m_versionBtn = new QPushButton(QString("v%1 · About").arg(BioSync::version()));
    m_versionBtn->setCursor(Qt::PointingHandCursor);
    m_versionBtn->setFlat(true);
    m_versionBtn->setFixedHeight(52);
    connect(m_versionBtn, &QPushButton::clicked, this, &MainWindow::onShowAbout);

    auto *sideLayout = new QVBoxLayout(m_sidebar);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(0);
    sideLayout->addWidget(m_logoArea);

    auto *navSpacer = new QWidget();
    navSpacer->setFixedHeight(20);
    sideLayout->addWidget(navSpacer);

    for (int i = 0; i < 4; ++i) {
        if (i == 3) sideLayout->addStretch();
        sideLayout->addWidget(m_navBtns[i]);
    }
    sideLayout->addWidget(m_versionBtn);

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

    m_contentArea = new QWidget();
    auto *contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);
    contentLayout->addWidget(m_stack);

    // ── Root layout ──────────────────────────────────────────────────────────
    auto *root = new QWidget();
    auto *rootLayout = new QHBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addWidget(m_sidebar);
    rootLayout->addWidget(m_contentArea);
    setCentralWidget(root);

    // ── Status bar ───────────────────────────────────────────────────────────
    m_statusServer  = new QLabel("● Server starting...");
    m_statusApi     = new QLabel("○ API unknown");
    m_statusPending = new QLabel("0 pending");

    m_statusDot1 = new QLabel("|");
    m_statusDot2 = new QLabel("|");

    statusBar()->addWidget(m_statusServer);
    statusBar()->addWidget(m_statusDot1);
    statusBar()->addWidget(m_statusApi);
    statusBar()->addWidget(m_statusDot2);
    statusBar()->addWidget(m_statusPending);

    connect(m_attendancePage, &AttendancePage::retryFailedRequested,
            this, &MainWindow::onRetryFailedRequested);
    connect(m_attendancePage, &AttendancePage::retryOneRequested,
            this, &MainWindow::onRetryOneRequested);

    applyTheme();

    // ── Signal connections ───────────────────────────────────────────────────
    // Engine events (only fire while this window runs the server) — just make the UI snappier;
    // everything is also in the shared DB, which refreshFromDb() mirrors either way.
    connect(m_engine, &Engine::attendanceStored, this, [this](int) { refreshFromDb(); });
    connect(m_engine, &Engine::deviceRegistered, this, [this](const QString &serial, const QString &ip) {
        m_devicesPage->refresh();
        m_logsPage->refreshDeviceFilter();
        m_trayIcon->showMessage("BioSync — New Device Detected",
            QString("Device %1 (%2) registered automatically.").arg(serial, ip),
            QSystemTrayIcon::Information, 4000);
    });

    connect(m_devicesPage, &DevicesPage::devicesChanged, this, [this]() {
        m_db->setSetting("config_rev", QString::number(QDateTime::currentMSecsSinceEpoch()));
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

void MainWindow::applyTheme() {
    const ThemePalette &t = Theme::current();

    m_sidebar->setStyleSheet(QString(
        "background:%1; border-right:1px solid %2;").arg(t.sidebarBg.name(), t.border.name()));
    m_logoArea->setStyleSheet(QString(
        "background:transparent; border-bottom:1px solid %1;").arg(t.border.name()));
    m_logoMark->setStyleSheet(QString(
        "background:%1; color:#FFFFFF; border-radius:8px; font-size:15px; font-weight:800;")
        .arg((t.isDark ? t.accent : t.textPrimary).name()));
    m_logoTitle->setText(QString(
        "<span style='color:%1; font-size:18px; font-weight:700;'>Bio</span>"
        "<span style='color:%2; font-size:18px; font-weight:800;'>Sync</span>")
        .arg(t.textPrimary.name(), t.accent.name()));
    m_versionBtn->setStyleSheet(QString(
        "QPushButton{color:%1; font-size:11px; font-weight:600; border:none; "
        "  border-top:1px solid %2; background:transparent;}"
        "QPushButton:hover{color:%3; background:%4;}")
        .arg(t.textDisabled.name(), t.border.name(), t.accent.name(), t.sidebarHover.name()));
    m_contentArea->setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));
    m_statusDot1->setStyleSheet(QString("color:%1;").arg(t.border.name()));
    m_statusDot2->setStyleSheet(QString("color:%1;").arg(t.border.name()));

    for (int i = 0; i < 4; ++i) m_navBtns[i]->update();

    setStatusServer(m_statusServer->text(), m_serverTone);
    setStatusApi(m_statusApi->text(), m_apiTone);

    if (m_trayIcon) {
        auto *trayMenu = m_trayIcon->contextMenu();
        const auto actions = trayMenu->actions();
        if (actions.size() >= 3) {
            actions[0]->setIcon(Icons::icon(Icons::Glyph::TrayShow, t.textPrimary, 16));
            actions[2]->setIcon(Icons::icon(Icons::Glyph::Quit, t.danger, 16));
        }
    }
}

void MainWindow::onRetryFailedRequested() {
    const int failed = m_db->getFailedCount();
    if (failed == 0) return;
    m_db->retryFailedRecords();
    onLogMessage({}, QString("Retrying %1 failed attendance record(s)...").arg(failed));
    m_pusher->pushNow();
    m_attendancePage->refresh();
}

void MainWindow::onRetryOneRequested(int recordId) {
    m_db->retryFailedRecords({recordId});
    onLogMessage({}, QString("Retrying attendance record #%1...").arg(recordId));
    m_pusher->pushNow();
    m_attendancePage->refresh();
}

void MainWindow::setupTray() {
    m_trayIcon = new QSystemTrayIcon(QIcon(":/assets/images/icon.ico"), this);
    m_trayIcon->setToolTip("BioSync — Attendance Manager");

    auto *trayMenu = new QMenu(this);
    trayMenu->addAction(Icons::icon(Icons::Glyph::TrayShow, Theme::current().textPrimary, 16),
                         "Show BioSync", this, &MainWindow::showAndRaise);
    trayMenu->addSeparator();
    trayMenu->addAction(Icons::icon(Icons::Glyph::Quit, Theme::current().danger, 16),
                         "Quit", qApp, &QApplication::quit);

    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);
    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
}

void MainWindow::showAndRaise() {
    if (isMinimized()) showNormal(); else show();
    raise();
    activateWindow();
}

void MainWindow::reloadApiConfig() {
    QString apiUrl = m_db->getSetting("api_url");
    QString apiKey = m_db->getSetting("api_key");
    long long instId = m_db->getSetting("institution_id", "0").toLongLong();
    m_pusher->setConfig(apiUrl, apiKey, instId);
}

// ── Server ownership ─────────────────────────────────────────────────────────

void MainWindow::supervise() {
    const quint16 port = static_cast<quint16>(m_db->getSetting("server_port", "8085").toUShort());
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool serviceWaiting = now - m_db->getSetting("service_waiting", "0").toLongLong() < 10000;

    if (m_engine->isRunning()) {
        if (serviceWaiting) {
            // The background service started and wants the port: it is the preferred owner (it
            // keeps running when this window is closed), so hand over. It binds within seconds.
            onLogMessage({}, "BioSync background service is starting — handing the attendance server over to it.");
            m_engine->stop();
            setMode(ServerMode::Service, port);
        } else {
            setMode(ServerMode::Engine, m_engine->port());
        }
        return;
    }

    if (m_userStopped) { setMode(ServerMode::Stopped, 0); return; }
    if (m_probing) return;

    // Before touching the port, ask whoever is on it whether it is BioSync. If it is (normally the
    // background service), just mirror it — never try to bind and never report "address in use".
    probePort(port, [this, port, serviceWaiting](bool isBioSync, QString who) {
        if (m_engine->isRunning() || m_userStopped) return;
        if (isBioSync) { setMode(ServerMode::Service, port, who); return; }
        if (serviceWaiting) { setMode(ServerMode::ServiceWaiting, port); return; }
        // The service is alive but briefly not listening (restarting / switching port): leave the
        // port to it. Only take over once its heartbeat has stopped, i.e. it is really gone.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (m_db->getSetting("engine_role") == "service" &&
            now - m_db->getSetting("engine_heartbeat", "0").toLongLong() < 3 * Engine::HEARTBEAT_MS) {
            setMode(ServerMode::Service, port);
            return;
        }
        if (m_engine->start()) { setMode(ServerMode::Engine, port); return; }
        setMode(ServerMode::Conflict, port, m_engine->lastErrorString());
    });
}

void MainWindow::probePort(quint16 port, std::function<void(bool, QString)> done) {
    m_probing = true;
    QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/biosync/ping").arg(port)));
    req.setTransferTimeout(1500);
    QNetworkReply *reply = m_probeNam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, done]() {
        reply->deleteLater();
        m_probing = false;
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const bool isBioSync = reply->error() == QNetworkReply::NoError && body.startsWith("BIOSYNC/");
        done(isBioSync, body);
    });
}

static QColor toneColor(MainWindow::Tone tone) {
    const ThemePalette &t = Theme::current();
    switch (tone) {
    case MainWindow::Tone::Ok:    return t.success;
    case MainWindow::Tone::Warn:  return t.warning;
    case MainWindow::Tone::Error: return t.danger;
    default:                      return t.textSecondary;
    }
}

void MainWindow::setStatusServer(const QString &text, Tone tone) {
    m_serverTone = tone;
    m_statusServer->setText(text);
    m_statusServer->setStyleSheet(QString("color:%1; font-weight:600;").arg(toneColor(tone).name()));
}

void MainWindow::setStatusApi(const QString &text, Tone tone) {
    m_apiTone = tone;
    m_statusApi->setText(text);
    m_statusApi->setStyleSheet(QString("color:%1; font-weight:600;").arg(toneColor(tone).name()));
}

void MainWindow::setMode(ServerMode mode, quint16 port, const QString &detail) {
    const bool changed = mode != m_mode;
    m_mode = mode;
    const QString p = QString::number(port);

    switch (mode) {
    case ServerMode::Starting:
        break;
    case ServerMode::Engine:
        setStatusServer(QString("● Server on :%1").arg(p), Tone::Ok);
        m_settingsPage->updateServerStatus(true, port);
        if (changed) onLogMessage({}, QString("Attendance server is running in this window on port %1 "
                                              "(no background service is running).").arg(p));
        break;
    case ServerMode::Service:
        setStatusServer(QString("● Background service on :%1").arg(p), Tone::Ok);
        m_settingsPage->showServerState(
            QString("● Running in the BioSync background service on port %1").arg(p), SettingsPage::StateTone::Ok,
            /*start*/false, /*stop*/false, /*restart*/true);
        if (changed) onLogMessage({}, QString("The BioSync background service is running the attendance "
                                              "server on port %1 — this window shows its live status.").arg(p));
        break;
    case ServerMode::ServiceWaiting:
        setStatusServer(QString("⚠ Waiting for port %1").arg(p), Tone::Warn);
        m_settingsPage->showServerState(
            QString("⚠ Background service is waiting for port %1 — another program is using it").arg(p),
            SettingsPage::StateTone::Warn, false, false, false);
        if (changed) onLogMessage({}, QString("Port %1 is used by another program; the BioSync background "
                                              "service will start as soon as it is free. Change the port in "
                                              "Settings if this persists.").arg(p));
        break;
    case ServerMode::Stopped:
        setStatusServer("✕ Server stopped", Tone::Error);
        m_settingsPage->updateServerStatus(false, 0);
        break;
    case ServerMode::Conflict:
        setStatusServer(QString("✕ Port %1 used by another program").arg(p), Tone::Error);
        m_settingsPage->showServerState(
            QString("✕ Port %1 is used by another program (not BioSync)").arg(p), SettingsPage::StateTone::Error,
            /*start*/true, false, false);
        if (changed) onLogMessage({}, QString("Attendance server can't start: port %1 is used by another "
                                              "program (%2). Close it or choose a different port in Settings.")
                                              .arg(p, detail));
        break;
    }
}

// ── Live view of the shared DB ───────────────────────────────────────────────

void MainWindow::refreshFromDb() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Devices: online = seen by the server owner within the last 2 minutes.
    const QList<Device> devices = m_db->getAllDevices();
    if (devices.size() != m_lastDeviceCount) {
        m_lastDeviceCount = devices.size();
        m_devicesPage->refresh();
        m_logsPage->refreshDeviceFilter();
        for (auto it = m_deviceOnlineMap.begin(); it != m_deviceOnlineMap.end(); ++it)
            m_devicesPage->markDeviceOnline(it.key(), it.value());
    }
    const auto seen = m_db->getDeviceSeen();
    for (const Device &d : devices) {
        const auto it = seen.constFind(d.serialNumber);
        const bool online = d.isActive && it != seen.constEnd() && now - it->lastSeenMs < 120000;
        if (online != m_deviceOnlineMap.value(d.serialNumber, false)) {
            m_deviceOnlineMap[d.serialNumber] = online;
            m_devicesPage->markDeviceOnline(d.serialNumber, online);
        }
        if (it != seen.constEnd()) m_devicesPage->setDeviceSocketIp(d.serialNumber, it->ip);
    }

    // Punches + pending count.
    const int maxId = m_db->maxAttendanceId();
    const int pending = m_db->getPendingCount();
    if (maxId != m_lastAttendanceId || pending != m_lastPending) {
        m_lastAttendanceId = maxId;
        m_lastPending = pending;
        if (m_stack->currentWidget() == m_attendancePage) m_attendancePage->refresh();
    }
    m_statusPending->setText(QString("%1 pending").arg(pending));

    // API status, as last seen by whoever runs the engine.
    const bool engineAlive =
        now - m_db->getSetting("engine_heartbeat", "0").toLongLong() < 3 * Engine::HEARTBEAT_MS;
    if (!engineAlive) {
        setStatusApi("○ API idle", Tone::Muted);
    } else if (m_db->getSetting("api_online") == "1") {
        setStatusApi("● API connected", Tone::Ok);
    } else {
        setStatusApi("○ API offline", Tone::Warn);
    }

    m_logsPage->tail();
}

void MainWindow::onScanNetwork() {
    QSet<QString> existing;
    for (const Device &d : m_db->getAllDevices())
        existing.insert(d.serialNumber);

    DeviceDiscoveryDialog dlg(existing, this);
    if (dlg.exec() == QDialog::Accepted) {
        int added = 0;
        for (const Device &d : dlg.selectedDevices()) {
            if (m_db->addDevice(d) > 0) added++;
        }
        if (added > 0) {
            m_db->setSetting("config_rev", QString::number(QDateTime::currentMSecsSinceEpoch()));
            m_devicesPage->refresh();
            m_logsPage->refreshDeviceFilter();
            QMessageBox::information(this, "Devices Added",
                QString("%1 device(s) added successfully.").arg(added));
        }
    }
}

// Logs go to the shared DB; the Logs page tails it (so service and window logs both show up).
void MainWindow::onLogMessage(const QString &deviceSerial, const QString &message) {
    m_db->appendLog(deviceSerial, message);
    m_logsPage->tail();
}

void MainWindow::onSettingsSaved() {
    reloadApiConfig();
    supervise();   // a port change takes effect right away
}

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
    m_db->clearDeviceSeen(serial);
    m_devicesPage->markDeviceOnline(serial, false);
    onLogMessage(serial, QString("Reconnect requested for SN=%1 — marked offline, waiting for re-connect.").arg(serial));
}

void MainWindow::onRestartDevice(const QString &serial) {
    // Queue a REBOOT command delivered on the device's next heartbeat poll
    // Queued in the shared DB: whichever BioSync process runs the server delivers it.
    m_db->queueDeviceCommand(serial, "REBOOT");
    m_deviceOnlineMap[serial] = false;
    m_db->clearDeviceSeen(serial);
    m_devicesPage->markDeviceOnline(serial, false);
    onLogMessage(serial, QString("Restart command queued for SN=%1 — delivered on its next poll.").arg(serial));
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

    // Queue the ADMS DATA QUERY in the shared DB; the server owner delivers it, receives the
    // templates and uploads them. The dialog follows that command's progress.
    const int cmdId = m_db->queueDeviceCommand(serial, "DATA QUERY FINGERTMP");
    if (cmdId <= 0) {
        QMessageBox::warning(this, "Biometric Backup", "Could not queue the backup request.");
        return;
    }

    auto *dlg = new BiometricBackupDialog(serial, d.name, m_db, cmdId, this);
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

    onLogMessage(serial, QString("Biometric backup initiated for SN=%1 — "
                                 "waiting for device to upload templates.").arg(serial));
}

void MainWindow::onRestoreTemplates(const QString &serial) {
    Device d = m_db->getDeviceBySerial(serial);

    QMessageBox mb(this);
    mb.setWindowTitle("Restore Biometric Templates");
    mb.setText(QString("Restore backed-up templates to device %1 (%2)?").arg(d.name, serial));
    mb.setInformativeText(
        "Backed-up templates will be fetched from the server and pushed to the device "
        "on its next heartbeat poll via ADMS DATA UPDATE command.");
    mb.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    mb.setIcon(QMessageBox::Question);
    for (auto *btn : mb.findChildren<QPushButton*>())
        if (mb.standardButton(btn) == QMessageBox::Ok) btn->setProperty("accent", true);
    if (mb.exec() != QMessageBox::Ok) return;

    onLogMessage(serial, QString("Fetching templates from server for SN=%1…").arg(serial));

    m_pusher->fetchBiometricTemplates(serial, [this, serial](QList<BiometricTemplate> templates) {
        if (templates.isEmpty()) {
            QMessageBox::warning(this, "Restore Templates",
                QString("No backed-up templates found on the server for device %1.").arg(serial));
            onLogMessage(serial, QString("Restore cancelled — no templates on server for SN=%1.").arg(serial));
            return;
        }
        m_db->queueDeviceCommand(serial, "DATA UPDATE FINGERTMP",
                                 ZkAdmsServer::buildTemplateRestoreBody(templates));
        onLogMessage(serial, QString("Restore of %1 template(s) queued for SN=%2.")
                                 .arg(templates.size()).arg(serial));
        m_trayIcon->showMessage("BioSync — Restore Queued",
            QString("%1 template(s) will be pushed to device %2 on next poll.")
                .arg(templates.size()).arg(serial),
            QSystemTrayIcon::Information, 4000);
    });
}

void MainWindow::onServerStart() {
    m_userStopped = false;
    supervise();
}

void MainWindow::onServerStop() {
    if (!m_engine->isRunning()) return;   // the background service can't be stopped from here
    m_userStopped = true;
    m_engine->stop();
    setMode(ServerMode::Stopped, 0);
}

void MainWindow::onServerRestart() {
    if (m_mode == ServerMode::Service) {
        // The background service restarts its own server when it sees this (within ~2s).
        m_db->setSetting("engine_restart_request", QString::number(QDateTime::currentMSecsSinceEpoch()));
        const QString msg = "Restart sent to the BioSync background service.";
        m_settingsPage->flashServerAction(msg, true);
        onLogMessage({}, msg);
        return;
    }

    m_settingsPage->flashServerAction("Restarting attendance server…", true);
    m_engine->stop();
    m_userStopped = false;
    const bool ok = m_engine->start();
    const quint16 port = m_engine->port();
    if (ok) setMode(ServerMode::Engine, port);
    else supervise();
    const QString msg = ok ? QString("Attendance server restarted on port %1.").arg(port)
                           : "Attendance server failed to restart — check the port setting.";
    m_settingsPage->flashServerAction(msg, ok);
    onLogMessage({}, msg);
    if (m_trayIcon && m_trayIcon->isVisible())
        m_trayIcon->showMessage("BioSync — Server Restart", msg,
            ok ? QSystemTrayIcon::Information : QSystemTrayIcon::Warning, 3000);
}

void MainWindow::onNavItemChanged(int row) {
    m_stack->setCurrentIndex(row);
    for (int i = 0; i < 4; ++i) m_navBtns[i]->setChecked(i == row);
    if (row == 1) m_attendancePage->refresh();
    else if (row == 2) m_logsPage->refreshDeviceFilter();
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) showAndRaise();
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

// ── About + update checking ──────────────────────────────────────────────────

static int compareVersions(const QString &a, const QString &b) {
    const QStringList pa = a.split('.'), pb = b.split('.');
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int va = i < pa.size() ? pa[i].toInt() : 0;
        const int vb = i < pb.size() ? pb[i].toInt() : 0;
        if (va != vb) return va < vb ? -1 : 1;
    }
    return 0;
}

void MainWindow::onShowAbout() {
    const ThemePalette &t = Theme::current();
    QDialog dlg(this);
    dlg.setWindowTitle("About BioSync");
    dlg.setMinimumWidth(420);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setContentsMargins(24, 22, 24, 20);
    lay->setSpacing(6);

    auto *title = new QLabel(BioSync::appName());
    title->setStyleSheet(QString("font-size:22px; font-weight:800; color:%1;").arg(t.accent.name()));
    lay->addWidget(title);

    auto *ver = new QLabel(QString("Version %1  ·  by %2").arg(BioSync::version(), BioSync::publisher()));
    ver->setStyleSheet("font-size:13px; color:palette(mid);");
    lay->addWidget(ver);
    lay->addSpacing(10);

    const bool signedBuild = BioSync::isSigned();
    auto *sig = new QLabel(signedBuild
        ? QString("✓  Official signed build  ·  key %1").arg(BioSync::secretFingerprint())
        : "⚠  Unsigned build — RiTEMS will reject its attendance data");
    sig->setStyleSheet(QString("font-size:12.5px; font-weight:600; color:%1;")
                           .arg((signedBuild ? t.success : t.danger).name()));
    sig->setWordWrap(true);
    lay->addWidget(sig);

    const QString server = m_db->getSetting("api_url").trimmed();
    auto *srv = new QLabel(QString("Server: %1").arg(server.isEmpty() ? "(not configured)" : server));
    srv->setStyleSheet("font-size:12px; color:palette(mid);");
    srv->setWordWrap(true);
    lay->addWidget(srv);

#if defined(Q_OS_WIN)
    auto *plat = new QLabel("Platform: Windows");
#else
    auto *plat = new QLabel("Platform: Linux");
#endif
    plat->setStyleSheet("font-size:12px; color:palette(mid);");
    lay->addWidget(plat);

    // Support contacts (shown only when configured in appinfo.cmake).
    QStringList support;
    if (!BioSync::supportEmail().isEmpty()) support << BioSync::supportEmail();
    if (!BioSync::supportPhone().isEmpty()) support << BioSync::supportPhone();
    if (!support.isEmpty()) {
        auto *sup = new QLabel("Support: " + support.join("  ·  "));
        sup->setStyleSheet("font-size:12px; color:palette(mid);");
        sup->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sup->setWordWrap(true);
        lay->addWidget(sup);
    }
    lay->addSpacing(16);

    auto *btnRow = new QHBoxLayout();
    auto *btnUpdate = new QPushButton("Check for updates");
    btnUpdate->setProperty("accent", true);
    btnUpdate->setCursor(Qt::PointingHandCursor);
    connect(btnUpdate, &QPushButton::clicked, &dlg, [this, &dlg]() { dlg.accept(); onCheckForUpdates(); });
    auto *btnClose = new QPushButton("Close");
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, &dlg, &QDialog::accept);
    btnRow->addWidget(btnUpdate);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);

    dlg.exec();
}

void MainWindow::onCheckForUpdates() { checkForUpdates(/*interactive=*/true); }

void MainWindow::checkForUpdates(bool interactive) {
    const QString base = m_db->getSetting("api_url").trimmed();
    if (base.isEmpty()) {
        if (interactive)
            QMessageBox::warning(this, "Check for updates",
                                 "Set the RiTEMS server URL in Settings first.");
        return;
    }
#if defined(Q_OS_WIN)
    const QString platform = "windows";
#else
    const QString platform = "linux";
#endif
    if (!m_updateNam) m_updateNam = new QNetworkAccessManager(this);
    QUrl url(base + "/api/v1/public/app-releases/latest");
    QUrlQuery q; q.addQueryItem("platform", platform); url.setQuery(q);

    QNetworkReply *reply = m_updateNam->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, interactive, base]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (interactive)
                QMessageBox::warning(this, "Check for updates",
                                     "Could not reach the update server:\n" + reply->errorString());
            return;
        }
        const QJsonObject data = QJsonDocument::fromJson(reply->readAll()).object().value("data").toObject();
        const QString latest = data.value("version").toString();
        if (latest.isEmpty()) {
            if (interactive)
                QMessageBox::information(this, "Check for updates", "No published version is available yet.");
            return;
        }
        if (compareVersions(BioSync::version(), latest) >= 0) {
            if (interactive)
                QMessageBox::information(this, "You're up to date",
                                        QString("BioSync v%1 is the latest version.").arg(BioSync::version()));
            return;
        }
        QString dl = data.value("downloadUrl").toString();
        if (dl.startsWith("/")) dl = base + dl;   // server returns a base-relative download path
        promptAndDownloadUpdate(latest, data.value("changelog").toString(),
                                dl, data.value("fileName").toString());
    });
}

void MainWindow::promptAndDownloadUpdate(const QString &version, const QString &changelog,
                                         const QString &downloadUrl, const QString &fileName) {
    if (downloadUrl.isEmpty()) return;
    QMessageBox box(this);
    box.setWindowTitle("Update available");
    box.setIcon(QMessageBox::Information);
    box.setText(QString("<b>BioSync v%1 is available.</b><br>You have v%2.").arg(version, BioSync::version()));
    if (!changelog.trimmed().isEmpty()) box.setInformativeText(changelog.trimmed());
    QPushButton *dl = box.addButton("Download && Install", QMessageBox::AcceptRole);
    box.addButton("Later", QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != dl) return;

    const QString saveName = fileName.isEmpty() ? QString("BioSync_Update_%1").arg(version) : fileName;
    const QString savePath = QDir(QDir::tempPath()).filePath(saveName);

    auto *prog = new QProgressDialog("Downloading update…", "Cancel", 0, 100, this);
    prog->setWindowTitle("Updating BioSync");
    prog->setWindowModality(Qt::WindowModal);
    prog->setMinimumDuration(0);
    prog->setValue(0);

    QNetworkReply *reply = m_updateNam->get(QNetworkRequest(QUrl(downloadUrl)));
    connect(reply, &QNetworkReply::downloadProgress, prog, [prog](qint64 got, qint64 total) {
        if (total > 0) prog->setValue(int(got * 100 / total));
    });
    connect(prog, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, prog, savePath]() {
        reply->deleteLater();
        prog->close();
        prog->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::warning(this, "Update failed", "Download failed:\n" + reply->errorString());
            return;
        }
        QSaveFile f(savePath);
        if (!f.open(QIODevice::WriteOnly) || f.write(reply->readAll()) < 0 || !f.commit()) {
            QMessageBox::warning(this, "Update failed", "Could not save the installer to:\n" + savePath);
            return;
        }
        QMessageBox::information(this, "Ready to install",
            "The update was downloaded. The installer will now open — follow its steps to finish "
            "updating, then BioSync will restart.");
        // Hand the installer to the OS (Windows: runs the .exe; Linux: opens the .deb installer),
        // then quit so it can replace the running binary.
        QDesktopServices::openUrl(QUrl::fromLocalFile(savePath));
        QTimer::singleShot(1500, qApp, &QApplication::quit);
    });
}
