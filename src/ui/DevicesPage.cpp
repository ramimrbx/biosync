#include "DevicesPage.h"
#include "AddDeviceDialog.h"
#include "Theme.h"
#include "Icons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QDateTime>
#include <QPushButton>
#include <QMenu>
#include <QAction>

// Column indices
enum Col { ColStatus=0, ColName, ColSerial, ColDeviceIp, ColSocketIp, ColPort, ColLocation, ColLastSeen, ColCount };

DevicesPage::DevicesPage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    // ── Page header ──────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setObjectName("devHeader");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);

    auto *titleBlock = new QVBoxLayout();
    auto *titleLabel = new QLabel("Devices");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700;");
    auto *subLabel = new QLabel("Manage ZKTeco attendance devices");
    subLabel->setStyleSheet("font-size:12px; color:palette(mid);");
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);

    auto *btnScan = new QPushButton(" Scan Network");
    btnScan->setProperty("accent", true);
    btnScan->setIcon(Icons::icon(Icons::Glyph::Search, Qt::white, 15, devicePixelRatioF()));
    btnScan->setCursor(Qt::PointingHandCursor);

    auto *btnAdd = new QPushButton(" Add Manually");
    btnAdd->setIcon(Icons::icon(Icons::Glyph::Add, Theme::current().textPrimary, 15, devicePixelRatioF()));
    btnAdd->setCursor(Qt::PointingHandCursor);

    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();
    hdrLayout->addWidget(btnScan);
    hdrLayout->addWidget(btnAdd);

    // ── Stat cards ───────────────────────────────────────────────────────────
    const ThemePalette &t0 = Theme::current();
    m_cardTotal  = makeStatCard("Total", "0",  t0.accent.name());
    m_cardOnline = makeStatCard("Online", "0", t0.success.name());
    m_cardOff    = makeStatCard("Offline", "0", t0.danger.name());

    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(16);
    cardsRow->addWidget(m_cardTotal);
    cardsRow->addWidget(m_cardOnline);
    cardsRow->addWidget(m_cardOff);
    cardsRow->addStretch();

    // ── Table toolbar ────────────────────────────────────────────────────────
    m_btnEdit   = new QPushButton(" Edit");
    m_btnRemove = new QPushButton(" Remove");
    m_btnEdit->setIcon(Icons::icon(Icons::Glyph::Edit, Theme::current().textPrimary, 14, devicePixelRatioF()));
    m_btnRemove->setIcon(Icons::icon(Icons::Glyph::Delete, Theme::current().danger, 14, devicePixelRatioF()));
    m_btnRemove->setProperty("danger", true);
    m_btnEdit->setCursor(Qt::PointingHandCursor);
    m_btnRemove->setCursor(Qt::PointingHandCursor);

    auto *tableToolbar = new QHBoxLayout();
    tableToolbar->addStretch();
    tableToolbar->addWidget(m_btnEdit);
    tableToolbar->addWidget(m_btnRemove);

    // ── Table (8 columns) ────────────────────────────────────────────────────
    m_table = new QTableWidget(0, ColCount);
    m_table->setHorizontalHeaderLabels(
        {"Status","Name","Serial","Device IP","Socket IP","Port","Location","Last Seen"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColStatus, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(ColPort,   QHeaderView::Fixed);
    m_table->setColumnWidth(ColStatus, 90);
    m_table->setColumnWidth(ColPort,   70);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    // ── Assemble ─────────────────────────────────────────────────────────────
    auto *body = new QVBoxLayout();
    body->setContentsMargins(28, 20, 28, 20);
    body->setSpacing(16);
    body->addLayout(cardsRow);
    body->addLayout(tableToolbar);
    body->addWidget(m_table);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addLayout(body);

    // ── Connections ──────────────────────────────────────────────────────────
    connect(btnScan,    &QPushButton::clicked, this, &DevicesPage::scanNetworkRequested);
    connect(btnAdd,     &QPushButton::clicked, this, &DevicesPage::onAddDevice);
    connect(m_btnEdit,  &QPushButton::clicked, this, &DevicesPage::onEditDevice);
    connect(m_btnRemove,&QPushButton::clicked, this, &DevicesPage::onRemoveDevice);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &DevicesPage::onContextMenuRequested);

    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &DevicesPage::applyTheme);

    refresh();
}

void DevicesPage::applyTheme() {
    const ThemePalette &t = Theme::current();
    setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));
    findChild<QWidget*>("devHeader")->setStyleSheet(
        QString("#devHeader{background:%1; border-bottom:1.5px solid %2;}")
            .arg(t.headerBg.name(), t.border.name()));

    for (QWidget *card : {m_cardTotal, m_cardOnline, m_cardOff})
        card->setStyleSheet(QString("background:%1; border-radius:12px; border:1.5px solid %2;")
                                 .arg(t.cardBg.name(), t.border.name()));
    populateTable(m_db->getAllDevices());
}

QWidget *DevicesPage::makeStatCard(const QString &label, const QString &value, const QString &color) {
    const ThemePalette &t = Theme::current();
    auto *card = new QWidget();
    card->setFixedSize(130, 68);
    card->setStyleSheet(QString(
        "background:%1; border-radius:12px; border:1.5px solid %2;").arg(t.cardBg.name(), t.border.name()));

    auto *val = new QLabel(value);
    val->setObjectName("val");
    val->setStyleSheet(QString("font-size:22px; font-weight:700; color:%1;").arg(color));
    auto *lbl = new QLabel(label);
    lbl->setStyleSheet("font-size:11px; color:palette(mid); font-weight:500;");

    auto *lay = new QVBoxLayout(card);
    lay->setContentsMargins(14, 10, 14, 10);
    lay->setSpacing(2);
    lay->addWidget(val);
    lay->addWidget(lbl);
    return card;
}

void DevicesPage::updateStatCards() {
    auto devices = m_db->getAllDevices();
    int total  = devices.size();
    int online = 0;
    for (const auto &d : devices)
        if (m_onlineSet.contains(d.serialNumber)) online++;

    auto setVal = [](QWidget *card, const QString &v) {
        if (auto *lbl = card->findChild<QLabel*>("val")) lbl->setText(v);
    };
    setVal(m_cardTotal,  QString::number(total));
    setVal(m_cardOnline, QString::number(online));
    setVal(m_cardOff,    QString::number(total - online));
}

void DevicesPage::refresh() {
    auto devices = m_db->getAllDevices();
    populateTable(devices);
    updateStatCards();
}

static QWidget *makeStatusPill(bool online) {
    const ThemePalette &t = Theme::current();
    auto *statusWidget = new QWidget();
    auto *statusLayout = new QHBoxLayout(statusWidget);
    statusLayout->setContentsMargins(8, 0, 8, 0);
    statusLayout->setSpacing(5);
    auto *iconLbl = new QLabel();
    iconLbl->setPixmap(Icons::pixmap(online ? Icons::Glyph::Online : Icons::Glyph::Offline,
                                      online ? t.success : t.textDisabled, 10,
                                      statusWidget->devicePixelRatioF()));
    auto *pill = new QLabel(online ? "Online" : "Offline");
    pill->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
                             .arg((online ? t.success : t.textSecondary).name()));
    statusWidget->setStyleSheet(QString("background:%1; border-radius:10px;")
                                     .arg((online ? t.successBg : t.border).name()));
    statusLayout->addWidget(iconLbl, 0, Qt::AlignVCenter);
    statusLayout->addWidget(pill, 0, Qt::AlignVCenter);
    return statusWidget;
}

void DevicesPage::populateTable(const QList<Device> &devices) {
    m_table->setRowCount(0);
    m_serialToRow.clear();

    for (int row = 0; row < devices.size(); ++row) {
        const Device &d = devices[row];
        m_table->insertRow(row);
        m_serialToRow[d.serialNumber] = row;

        bool online = m_onlineSet.contains(d.serialNumber);
        m_table->setCellWidget(row, ColStatus, makeStatusPill(online));

        QColor textColor = d.isActive ? Theme::current().textPrimary : Theme::current().textDisabled;
        auto mkItem = [&](const QString &text) {
            auto *i = new QTableWidgetItem(text);
            i->setData(Qt::UserRole, d.id);
            i->setForeground(textColor);
            return i;
        };

        m_table->setItem(row, ColName,      mkItem(d.name));
        m_table->setItem(row, ColSerial,    mkItem(d.serialNumber));
        m_table->setItem(row, ColDeviceIp,  mkItem(d.ipAddress));
        m_table->setItem(row, ColSocketIp,  mkItem(m_socketIpMap.value(d.serialNumber, "—")));
        m_table->setItem(row, ColPort,      mkItem(QString::number(d.port)));
        m_table->setItem(row, ColLocation,  mkItem(d.location));
        m_table->setItem(row, ColLastSeen,  mkItem(
            d.lastSeen.isValid() ? d.lastSeen.toString("yyyy-MM-dd hh:mm") : "—"));
    }
}

void DevicesPage::markDeviceOnline(const QString &serial, bool online) {
    if (online) m_onlineSet.insert(serial); else m_onlineSet.remove(serial);
    updateStatCards();

    if (!m_serialToRow.contains(serial)) { refresh(); return; }
    int row = m_serialToRow[serial];

    m_table->setCellWidget(row, ColStatus, makeStatusPill(online));

    if (online && m_table->item(row, ColLastSeen))
        m_table->item(row, ColLastSeen)->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm"));
}

void DevicesPage::setDeviceSocketIp(const QString &serial, const QString &ip) {
    m_socketIpMap[serial] = ip;
    if (!m_serialToRow.contains(serial)) return;
    int row = m_serialToRow[serial];
    if (auto *item = m_table->item(row, ColSocketIp))
        item->setText(ip);
}

Device DevicesPage::selectedDevice() const {
    int row = m_table->currentRow();
    if (row < 0) return {};
    for (int col = 1; col < m_table->columnCount(); ++col) {
        auto *item = m_table->item(row, col);
        if (item) return m_db->getDevice(item->data(Qt::UserRole).toInt());
    }
    return {};
}

void DevicesPage::onContextMenuRequested(const QPoint &pos) {
    int row = m_table->rowAt(pos.y());
    if (row < 0) return;

    m_table->selectRow(row);
    Device d = selectedDevice();
    if (!d.id) return;

    bool online = m_onlineSet.contains(d.serialNumber);
    const ThemePalette &t = Theme::current();
    const qreal dpr = devicePixelRatioF();

    QMenu menu(this);

    // Activate / Deactivate
    if (d.isActive) {
        auto *act = menu.addAction(Icons::icon(Icons::Glyph::Pause, t.textPrimary, 15, dpr), "Deactivate");
        connect(act, &QAction::triggered, this, [this, d]() { emit deactivateDeviceRequested(d.id); });
    } else {
        auto *act = menu.addAction(Icons::icon(Icons::Glyph::Play, t.textPrimary, 15, dpr), "Activate");
        connect(act, &QAction::triggered, this, [this, d]() { emit activateDeviceRequested(d.id); });
    }

    // Reconnect — only when device is currently connected
    if (online) {
        auto *act = menu.addAction(Icons::icon(Icons::Glyph::Retry, t.textPrimary, 15, dpr), "Reconnect");
        connect(act, &QAction::triggered, this, [this, d]() { emit reconnectDeviceRequested(d.serialNumber); });
    }

    // Restart Device
    auto *actRestart = menu.addAction(Icons::icon(Icons::Glyph::Restart, t.textPrimary, 15, dpr), "Restart Device");
    connect(actRestart, &QAction::triggered, this, [this, d]() { emit restartDeviceRequested(d.serialNumber); });

    menu.addSeparator();

    // Biometric template operations
    auto *actBackup = menu.addAction(Icons::icon(Icons::Glyph::Upload, t.textPrimary, 15, dpr),
                                      "Backup Biometric Templates");
    connect(actBackup, &QAction::triggered, this, [this, d]() { emit backupTemplatesRequested(d.serialNumber); });

    auto *actRestore = menu.addAction(Icons::icon(Icons::Glyph::Download, t.textPrimary, 15, dpr),
                                       "Restore Biometric Templates");
    connect(actRestore, &QAction::triggered, this, [this, d]() { emit restoreTemplatesRequested(d.serialNumber); });

    menu.addSeparator();

    auto *actEdit = menu.addAction(Icons::icon(Icons::Glyph::Edit, t.textPrimary, 15, dpr), "Edit");
    connect(actEdit, &QAction::triggered, this, &DevicesPage::onEditDevice);

    auto *actDelete = menu.addAction(Icons::icon(Icons::Glyph::Delete, t.danger, 15, dpr), "Delete");
    actDelete->setProperty("danger", true);
    connect(actDelete, &QAction::triggered, this, &DevicesPage::onRemoveDevice);

    menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void DevicesPage::onAddDevice() {
    AddDeviceDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_db->addDevice(dlg.device()) > 0) { refresh(); emit devicesChanged(); }
        else QMessageBox::critical(this, "Error", "Failed to add device. Serial number may already exist.");
    }
}
void DevicesPage::onEditDevice() {
    Device d = selectedDevice();
    if (!d.id) { QMessageBox::information(this, "Edit", "Please select a device."); return; }
    AddDeviceDialog dlg(this, d);
    if (dlg.exec() == QDialog::Accepted) {
        if (m_db->updateDevice(dlg.device())) { refresh(); emit devicesChanged(); }
        else QMessageBox::critical(this, "Error", "Failed to update device.");
    }
}
void DevicesPage::onRemoveDevice() {
    Device d = selectedDevice();
    if (!d.id) { QMessageBox::information(this, "Remove", "Please select a device."); return; }
    if (QMessageBox::question(this, "Remove Device",
        QString("Remove \"%1\"? Attendance records are kept.").arg(d.name)) == QMessageBox::Yes) {
        m_db->removeDevice(d.id);
        m_onlineSet.remove(d.serialNumber);
        m_socketIpMap.remove(d.serialNumber);
        refresh();
        emit devicesChanged();
    }
}
