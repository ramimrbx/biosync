#include "SettingsPage.h"
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

static QString inputStyle() {
    return "QLineEdit, QSpinBox {"
           "  background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:8px;"
           "  padding:10px 12px; min-height:22px; font-size:14px; color:#111827;"
           "}"
           "QLineEdit:focus, QSpinBox:focus {"
           "  border-color:#8222E3;"
           "}"
           "QSpinBox::up-button, QSpinBox::down-button { width:20px; border-radius:4px; }";
}

static QString cardStyle() {
    return "QGroupBox {"
           "  background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:12px;"
           "  padding:20px; margin-top:8px; font-size:13px; font-weight:600; color:#111827;"
           "}"
           "QGroupBox::title { subcontrol-origin:margin; left:16px; padding:0 8px;"
           "  background:#FFFFFF; color:#111827; }";
}

SettingsPage::SettingsPage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    setStyleSheet("background:#F4F6FB;");

    // ── Header ────────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setStyleSheet("background:#FFFFFF; border-bottom:1.5px solid #E5E7EB;");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);
    auto *titleLabel = new QLabel("Settings");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700; color:#111827;");
    auto *subLabel = new QLabel("Configure server and API connection");
    subLabel->setStyleSheet("font-size:12px; color:#6B7280;");
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
    ipBanner->setStyleSheet(
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8222E3,stop:1 #A855F7);"
        "border-radius:12px;");
    ipBanner->setFixedHeight(64);
    auto *ipLayout = new QHBoxLayout(ipBanner);
    ipLayout->setContentsMargins(20, 0, 20, 0);
    auto *ipIcon = new QLabel("🖥");
    ipIcon->setStyleSheet("font-size:22px;");
    auto *ipTitle = new QLabel("This Machine's IP");
    ipTitle->setStyleSheet("color:rgba(255,255,255,0.75); font-size:11px;");
    auto *ipVal = new QLabel(localIps.isEmpty() ? "No network" : localIps.join("   "));
    ipVal->setStyleSheet("color:#FFFFFF; font-size:16px; font-weight:700;");
    auto *ipTextCol = new QVBoxLayout();
    ipTextCol->setSpacing(2);
    ipTextCol->addWidget(ipTitle);
    ipTextCol->addWidget(ipVal);
    ipLayout->addWidget(ipIcon);
    ipLayout->addSpacing(10);
    ipLayout->addLayout(ipTextCol);
    ipLayout->addStretch();
    auto *ipHint = new QLabel("← Configure this IP on your ZKTeco devices");
    ipHint->setStyleSheet("color:rgba(255,255,255,0.65); font-size:11px;");
    ipLayout->addWidget(ipHint);

    // ── BioSync Server control card ───────────────────────────────────────────
    m_serverStatusLabel = new QLabel("○ Unknown");
    m_serverStatusLabel->setStyleSheet("font-size:13px; font-weight:600; color:#6B7280;");

    m_btnStart = new QPushButton("▶  Start");
    m_btnStop  = new QPushButton("⏹  Stop");
    m_btnRestart = new QPushButton("↺  Restart");

    auto serverBtnStyle = [](const QString &bg, const QString &hover) {
        return QString("QPushButton { background:%1; color:#FFFFFF; border:none;"
                       "  padding:7px 16px; border-radius:8px; font-weight:600; font-size:12px; }"
                       "QPushButton:hover { background:%2; }"
                       "QPushButton:pressed { background:%2; }"
                       "QPushButton:disabled { background:#D1D5DB; }").arg(bg, hover);
    };
    m_btnStart->setStyleSheet(serverBtnStyle("#10B981", "#059669"));
    m_btnStop->setStyleSheet(serverBtnStyle("#EF4444", "#DC2626"));
    m_btnRestart->setStyleSheet(serverBtnStyle("#8222E3", "#6A1AB8"));
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
    serverControlGroup->setStyleSheet(cardStyle());
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

    m_autoStart = new QCheckBox("  Start BioSync automatically when Windows starts");
    m_autoStart->setCursor(Qt::PointingHandCursor);
    m_autoStart->setStyleSheet(
        "QCheckBox { font-size:13px; color:#111827; spacing:8px; }"
        "QCheckBox::indicator { width:18px; height:18px; border:1.5px solid #D1D5DB; border-radius:5px; background:#FFFFFF; }"
        "QCheckBox::indicator:checked { background:#8222E3; border-color:#8222E3;"
        "}");
    serverControlLayout->addWidget(m_autoStart);
    serverControlLayout->addWidget(hint("So the attendance server is running whenever this computer is on, even after a reboot."));

    // ── ADMS Server port card ─────────────────────────────────────────────────
    m_serverPort = new QSpinBox();
    m_serverPort->setRange(1, 65535);
    m_serverPort->setValue(8085);
    m_serverPort->setStyleSheet(inputStyle() + "QSpinBox { max-width:120px; }");

    auto *serverGroup = new QGroupBox("ADMS Server");
    serverGroup->setStyleSheet(cardStyle());
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

    for (auto *le : {m_apiUrl, m_apiKey, m_institutionId})
        le->setStyleSheet(inputStyle());

    auto *apiGroup = new QGroupBox("RiTEMS Connection");
    apiGroup->setStyleSheet(cardStyle());
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
    auto *btnSave = new QPushButton("  Save Settings");
    btnSave->setFixedHeight(44);
    btnSave->setStyleSheet(
        "QPushButton { background:#8222E3; color:#FFFFFF; border:none;"
        "  padding:0 28px; border-radius:10px; font-size:13px; font-weight:700; }"
        "QPushButton:hover   { background:#6A1AB8; }"
        "QPushButton:pressed { background:#5a14a0; }");
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
    bodyWidget->setStyleSheet("background:transparent;");
    bodyWidget->setLayout(bodyRow);

    // …inside a scroll area, so every field is reachable on any window size (this was the
    // reason the API fields could not be clicked/typed on shorter windows).
    auto *scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea{background:#F4F6FB;border:none;}");
    scroll->setWidget(bodyWidget);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addWidget(scroll, 1);

    loadSettings();
}

void SettingsPage::updateServerStatus(bool running, quint16 port) {
    if (running) {
        m_serverStatusLabel->setText(QString("● Running on port %1").arg(port));
        m_serverStatusLabel->setStyleSheet("font-size:13px; font-weight:600; color:#10B981;");
        m_btnStart->setEnabled(false);
        m_btnStop->setEnabled(true);
        m_btnRestart->setEnabled(true);
    } else {
        m_serverStatusLabel->setText("○ Stopped");
        m_serverStatusLabel->setStyleSheet("font-size:13px; font-weight:600; color:#EF4444;");
        m_btnStart->setEnabled(true);
        m_btnStop->setEnabled(false);
        m_btnRestart->setEnabled(false);
    }
}

void SettingsPage::flashServerAction(const QString &message, bool ok) {
    m_actionToast->setText((ok ? "✓  " : "⚠  ") + message);
    m_actionToast->setStyleSheet(QString("font-size:12px; font-weight:600; padding:2px 0; color:%1;")
                                     .arg(ok ? "#10B981" : "#EF4444"));
    m_actionToast->setVisible(true);
    QTimer::singleShot(5000, m_actionToast, [this]() { m_actionToast->setVisible(false); });
}

QLabel *SettingsPage::styledLabel(const QString &text) {
    auto *lbl = new QLabel(text + ":");
    lbl->setStyleSheet("font-size:13px; color:#374151; font-weight:500;");
    return lbl;
}

QLabel *SettingsPage::hint(const QString &text) {
    auto *lbl = new QLabel(text);
    lbl->setWordWrap(true);
    lbl->setStyleSheet("font-size:11px; color:#9CA3AF;");
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

    // Apply launch-at-startup, and reflect what actually took effect.
    Autostart::setEnabled(m_autoStart->isChecked());
    m_autoStart->setChecked(Autostart::isEnabled());

    QMessageBox mb(this);
    mb.setWindowTitle("Settings Saved");
    mb.setText("Settings saved successfully.");
    mb.setInformativeText("Use Restart in the Server card to apply port changes immediately.");
    mb.setIcon(QMessageBox::Information);
    mb.setStyleSheet(
        "QMessageBox { background:#FFFFFF; }"
        "QMessageBox QPushButton { background:#8222E3; color:#FFFFFF; border-radius:8px;"
        "  padding:6px 20px; font-weight:600; }"
        "QMessageBox QPushButton:hover { background:#6A1AB8; }");
    mb.exec();

    emit settingsSaved();
}
