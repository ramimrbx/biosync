#include "SettingsPage.h"
#include "Theme.h"
#include "Icons.h"
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QTimer>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QAbstractSocket>
#include <QFrame>
#include <QCheckBox>
#include <QScrollArea>
#include <QSpacerItem>
#include "../service/Autostart.h"

SettingsPage::SettingsPage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    // ── Header ────────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setObjectName("settingsHeader");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);
    auto *titleLabel = new QLabel("Settings");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700;");
    auto *subLabel = new QLabel("Configure server and API connection");
    subLabel->setStyleSheet("font-size:12px; color:palette(mid);");
    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);
    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();

    // ── IP display ────────────────────────────────────────────────────────────
    QStringList localIps;
    for (const auto &iface : QNetworkInterface::allInterfaces()) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp) ||
            iface.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        for (const auto &entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                localIps << entry.ip().toString();
        }
    }

    auto *ipBanner = new QWidget();
    ipBanner->setObjectName("ipBanner");
    ipBanner->setFixedHeight(64);
    auto *ipLayout = new QHBoxLayout(ipBanner);
    ipLayout->setContentsMargins(20, 0, 20, 0);
    auto *ipIcon = new QLabel();
    ipIcon->setObjectName("ipIcon");
    ipIcon->setPixmap(Icons::pixmap(Icons::Glyph::Monitor, Theme::current().accent, 24, devicePixelRatioF()));
    auto *ipTitle = new QLabel("This Machine's IP");
    ipTitle->setObjectName("ipTitle");
    auto *ipVal = new QLabel(localIps.isEmpty() ? "No network" : localIps.join("   "));
    ipVal->setObjectName("ipVal");
    auto *ipTextCol = new QVBoxLayout();
    ipTextCol->setSpacing(2);
    ipTextCol->addWidget(ipTitle);
    ipTextCol->addWidget(ipVal);
    ipLayout->addWidget(ipIcon);
    ipLayout->addSpacing(10);
    ipLayout->addLayout(ipTextCol);
    ipLayout->addStretch();
    auto *ipHint = new QLabel("← Configure this IP on your ZKTeco devices");
    ipHint->setObjectName("ipHint");
    ipLayout->addWidget(ipHint);

    // ── BioSync Server control card ───────────────────────────────────────────
    m_serverStatusLabel = new QLabel("○ Unknown");

    m_btnStart   = new QPushButton(" Start");
    m_btnStop    = new QPushButton(" Stop");
    m_btnRestart = new QPushButton(" Restart");
    m_btnStart->setObjectName("btnStart");
    m_btnStop->setObjectName("btnStop");
    m_btnRestart->setObjectName("btnRestart");
    m_btnStart->setIcon(Icons::icon(Icons::Glyph::Play, Qt::white, 13, devicePixelRatioF()));
    m_btnStop->setIcon(Icons::icon(Icons::Glyph::Pause, Qt::white, 13, devicePixelRatioF()));
    m_btnRestart->setIcon(Icons::icon(Icons::Glyph::Restart, Qt::white, 13, devicePixelRatioF()));
    m_btnStart->setCursor(Qt::PointingHandCursor);
    m_btnStop->setCursor(Qt::PointingHandCursor);
    m_btnRestart->setCursor(Qt::PointingHandCursor);

    auto *serverBtnRow = new QHBoxLayout();
    serverBtnRow->setSpacing(8);
    serverBtnRow->addWidget(m_btnStart);
    serverBtnRow->addWidget(m_btnStop);
    serverBtnRow->addWidget(m_btnRestart);
    serverBtnRow->addStretch();

    auto *serverControlGroup = new QGroupBox("BioSync Server");
    auto *serverControlLayout = new QVBoxLayout(serverControlGroup);
    serverControlLayout->setContentsMargins(8, 16, 8, 8);
    serverControlLayout->setSpacing(12);

    auto *statusRow = new QHBoxLayout();
    statusRow->addWidget(styledLabel("Status"));
    statusRow->addSpacing(8);
    statusRow->addWidget(m_serverStatusLabel);
    statusRow->addStretch();
    serverControlLayout->addLayout(statusRow);
    serverControlLayout->addLayout(serverBtnRow);
    m_actionToast = new QLabel();
    m_actionToast->setStyleSheet("font-size:12px; font-weight:600; padding:2px 0;");
    m_actionToast->setVisible(false);
    serverControlLayout->addWidget(m_actionToast);
    serverControlLayout->addWidget(hint("Start/Stop the ADMS listener. Restart applies new port settings immediately."));

    // Attendance always runs in the background service at boot; this only opens the window.
    // Attendance always runs in the background service from power-on; this only adds the tray icon.
    m_autoStart = new QCheckBox("  Start BioSync in the system tray when I sign in");
    m_autoStart->setCursor(Qt::PointingHandCursor);
    serverControlLayout->addWidget(m_autoStart);
    serverControlLayout->addWidget(hint("So the attendance server is running whenever this computer is on, even after a reboot."));

    // ── ADMS Server port card ─────────────────────────────────────────────────
    m_serverPort = new QSpinBox();
    m_serverPort->setRange(1, 65535);
    m_serverPort->setValue(8085);
    m_serverPort->setMaximumWidth(120);

    auto *serverGroup = new QGroupBox("ADMS Server");
    auto *serverForm = new QFormLayout(serverGroup);
    serverForm->setLabelAlignment(Qt::AlignRight);
    serverForm->setSpacing(14);
    serverForm->setContentsMargins(8, 16, 8, 8);
    serverForm->addRow(styledLabel("Listen Port"), m_serverPort);
    serverForm->addRow("",
        hint("Set your ZKTeco device's ADMS server address to the IP above and this port."));

    // ── API Connection card ───────────────────────────────────────────────────
    m_apiUrl = new QLineEdit();
    m_apiUrl->setPlaceholderText("https://ritems.io");
    m_apiUrl->setClearButtonEnabled(true);
    m_apiKey = new QLineEdit();
    m_apiKey->setPlaceholderText("the biosync-api-key value");
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_institutionId = new QLineEdit();
    m_institutionId->setPlaceholderText("e.g. 1");
    m_institutionId->setClearButtonEnabled(true);

    auto *apiGroup = new QGroupBox("RiTEMS Connection");
    auto *apiForm = new QFormLayout(apiGroup);
    apiForm->setLabelAlignment(Qt::AlignRight);
    apiForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    apiForm->setSpacing(6);
    apiForm->setContentsMargins(8, 16, 8, 8);
    apiForm->addRow(styledLabel("Base URL"), m_apiUrl);
    apiForm->addRow("", hint("Your RiTEMS website address only — no path. e.g. https://ritems.io"));
    apiForm->addItem(new QSpacerItem(0, 8));
    apiForm->addRow(styledLabel("API Key"), m_apiKey);
    apiForm->addRow("", hint("Must match the key set in RiTEMS → Settings → Gateway → biosync-api-key."));
    apiForm->addItem(new QSpacerItem(0, 8));
    apiForm->addRow(styledLabel("Institution ID"), m_institutionId);
    apiForm->addRow("", hint("The numeric ID of the institution these devices belong to in RiTEMS."));

    // ── Save button ───────────────────────────────────────────────────────────
    auto *btnSave = new QPushButton(" Save Settings");
    btnSave->setProperty("accent", true);
    btnSave->setFixedHeight(44);
    btnSave->setCursor(Qt::PointingHandCursor);
    connect(btnSave, &QPushButton::clicked, this, &SettingsPage::onSave);

    // ── Server control connections ────────────────────────────────────────────
    connect(m_btnStart,   &QPushButton::clicked, this, &SettingsPage::serverStartRequested);
    connect(m_btnStop,    &QPushButton::clicked, this, &SettingsPage::serverStopRequested);
    connect(m_btnRestart, &QPushButton::clicked, this, &SettingsPage::serverRestartRequested);

    // ── Body ─────────────────────────────────────────────────────────────────
    // Centred, width-capped column of cards…
    auto *col = new QVBoxLayout();
    col->setSpacing(16);
    col->setContentsMargins(0, 0, 0, 0);
    col->addWidget(ipBanner);
    col->addWidget(serverControlGroup);
    col->addWidget(serverGroup);
    col->addWidget(apiGroup);
    col->addWidget(btnSave, 0, Qt::AlignLeft);
    col->addStretch();

    auto *colWrap = new QWidget();
    colWrap->setMaximumWidth(780);
    colWrap->setLayout(col);

    auto *bodyRow = new QHBoxLayout();
    bodyRow->setContentsMargins(28, 20, 28, 28);
    bodyRow->addStretch();
    bodyRow->addWidget(colWrap, 1);
    bodyRow->addStretch();

    auto *bodyWidget = new QWidget();
    bodyWidget->setObjectName("settingsBody");
    bodyWidget->setLayout(bodyRow);

    // …inside a scroll area, so every field is reachable on any window size (this was the
    // reason the API fields could not be clicked/typed on shorter windows).
    auto *scroll = new QScrollArea();
    scroll->setObjectName("settingsScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(bodyWidget);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addWidget(scroll, 1);

    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &SettingsPage::applyTheme);

    loadSettings();
}

void SettingsPage::applyTheme() {
    const ThemePalette &t = Theme::current();
    setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));
    findChild<QWidget*>("settingsHeader")->setStyleSheet(
        QString("#settingsHeader{background:%1; border-bottom:1.5px solid %2;}")
            .arg(t.headerBg.name(), t.border.name()));
    findChild<QWidget*>("settingsBody")->setStyleSheet("background:transparent;");
    findChild<QWidget*>("settingsScroll")->setStyleSheet(
        QString("QScrollArea{background:%1;border:none;}").arg(t.pageBg.name()));

    // Soft accent wash, matching ritems' InfoBanner.tsx convention, rather than a solid gradient.
    findChild<QWidget*>("ipBanner")->setStyleSheet(QString(
        "#ipBanner{background: %1; border-radius:12px;}").arg(t.accentSoft()));
    findChild<QLabel*>("ipTitle")->setStyleSheet(
        QString("color:%1; font-size:11px;").arg(t.textSecondary.name()));
    findChild<QLabel*>("ipVal")->setStyleSheet(
        QString("color:%1; font-size:16px; font-weight:700;").arg(t.accent.name()));
    findChild<QLabel*>("ipHint")->setStyleSheet(
        QString("color:%1; font-size:11px;").arg(t.textSecondary.name()));
    if (auto *ipIcon = findChild<QLabel*>("ipIcon"))
        ipIcon->setPixmap(Icons::pixmap(Icons::Glyph::Monitor, t.accent, 24, devicePixelRatioF()));

    auto serverBtnStyle = [](const QColor &bg, const QColor &hover, const QColor &disabled) {
        return QString("QPushButton { background:%1; color:#FFFFFF; border:none;"
                       "  padding:8px 16px; border-radius:12px; font-weight:600; font-size:12px; }"
                       "QPushButton:hover { background:%2; }"
                       "QPushButton:pressed { background:%2; }"
                       "QPushButton:disabled { background:%3; }")
            .arg(bg.name(), hover.name(), disabled.name());
    };
    findChild<QPushButton*>("btnStart")->setStyleSheet(
        serverBtnStyle(t.success, t.success.darker(115), t.border));
    findChild<QPushButton*>("btnStop")->setStyleSheet(
        serverBtnStyle(t.danger, t.danger.darker(115), t.border));
    findChild<QPushButton*>("btnRestart")->setStyleSheet(
        serverBtnStyle(t.accent, t.accentHover, t.border));

    if (m_customState)   // e.g. "running in the background service" — re-colour, keep text/buttons
        showServerState(m_serverStatusLabel->text(), m_stateTone, m_btnStart->isEnabled(),
                        m_btnStop->isEnabled(), m_btnRestart->isEnabled());
    else
        updateServerStatus(m_btnStop->isEnabled(), 0); // re-derive color only; text/port unaffected here
}

void SettingsPage::updateServerStatus(bool running, quint16 port) {
    m_customState = false;
    const ThemePalette &t = Theme::current();
    if (running) {
        if (port) m_serverStatusLabel->setText(QString("● Running on port %1").arg(port));
        m_serverStatusLabel->setStyleSheet(QString("font-size:13px; font-weight:600; color:%1;").arg(t.success.name()));
        m_btnStart->setEnabled(false);
        m_btnStop->setEnabled(true);
        m_btnRestart->setEnabled(true);
    } else {
        m_serverStatusLabel->setText("○ Stopped");
        m_serverStatusLabel->setStyleSheet(QString("font-size:13px; font-weight:600; color:%1;").arg(t.danger.name()));
        m_btnStart->setEnabled(true);
        m_btnStop->setEnabled(false);
        m_btnRestart->setEnabled(false);
    }
}

void SettingsPage::showServerState(const QString &text, StateTone tone,
                                   bool canStart, bool canStop, bool canRestart) {
    m_customState = true;
    m_stateTone = tone;
    const ThemePalette &t = Theme::current();
    const QColor c = tone == StateTone::Ok ? t.success : tone == StateTone::Warn ? t.warning : t.danger;
    m_serverStatusLabel->setText(text);
    m_serverStatusLabel->setStyleSheet(QString("font-size:13px; font-weight:600; color:%1;").arg(c.name()));
    m_btnStart->setEnabled(canStart);
    m_btnStop->setEnabled(canStop);
    m_btnRestart->setEnabled(canRestart);
}

void SettingsPage::flashServerAction(const QString &message, bool ok) {
    const ThemePalette &t = Theme::current();
    m_actionToast->setText((ok ? "✓  " : "⚠  ") + message);
    m_actionToast->setStyleSheet(QString("font-size:12px; font-weight:600; padding:2px 0; color:%1;")
                                     .arg((ok ? t.success : t.danger).name()));
    m_actionToast->setVisible(true);
    QTimer::singleShot(5000, m_actionToast, [this]() { m_actionToast->setVisible(false); });
}

QLabel *SettingsPage::styledLabel(const QString &text) {
    auto *lbl = new QLabel(text + ":");
    lbl->setStyleSheet("font-size:13px; font-weight:500;");
    return lbl;
}

QLabel *SettingsPage::hint(const QString &text) {
    auto *lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("font-size:11px; color:palette(mid);");
    return lbl;
}

void SettingsPage::loadSettings() {
    m_serverPort->setValue(m_db->getSetting("server_port", "8085").toInt());
    m_apiUrl->setText(m_db->getSetting("api_url"));
    m_apiKey->setText(m_db->getSetting("api_key"));
    m_institutionId->setText(m_db->getSetting("institution_id"));
    m_autoStart->setChecked(Autostart::isEnabled());
}

void SettingsPage::onSave() {
    m_db->setSetting("server_port", QString::number(m_serverPort->value()));
    QString apiUrl = m_apiUrl->text().trimmed();
    while (apiUrl.endsWith('/')) apiUrl.chop(1);
    m_db->setSetting("api_url",        apiUrl);
    m_db->setSetting("api_key",        m_apiKey->text().trimmed());
    m_db->setSetting("institution_id", m_institutionId->text().trimmed());
    // Tell whichever BioSync process runs the server (service or this window) to reload now.
    m_db->setSetting("config_rev", QString::number(QDateTime::currentMSecsSinceEpoch()));

    // Apply launch-at-startup, and reflect what actually took effect.
    Autostart::setEnabled(m_autoStart->isChecked());
    m_autoStart->setChecked(Autostart::isEnabled());

    QMessageBox mb(this);
    mb.setWindowTitle("Settings Saved");
    mb.setText("Settings saved successfully.");
    mb.setInformativeText("Changes are applied automatically within a few seconds.");
    mb.setIcon(QMessageBox::Information);
    mb.exec();

    emit settingsSaved();
}
