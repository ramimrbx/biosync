#include "DevicesPage.h"
#include "AddDeviceDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QLabel>
#include <QDateTime>
#include <QPushButton>
#include <QMenu>
#include <QAction>

// ── Shared style helpers ──────────────────────────────────────────────────────
static QString primaryBtn() {
    return "QPushButton {"
           "  background:#8222E3; color:#FFFFFF; border:none;"
           "  padding:8px 18px; border-radius:8px; font-weight:600; font-size:12px;"
           "}"
           "QPushButton:hover   { background:#6A1AB8; }"
           "QPushButton:pressed { background:#5a14a0; }";
}
static QString secondaryBtn() {
    return "QPushButton {"
           "  background:#FFFFFF; color:#374151; border:1.5px solid #E5E7EB;"
           "  padding:7px 16px; border-radius:8px; font-weight:500; font-size:12px;"
           "}"
           "QPushButton:hover   { background:#F9F5FF; border-color:#8222E3; color:#8222E3; }"
           "QPushButton:pressed { background:#EDE5FB; }";
}
static QString dangerBtn() {
    return "QPushButton {"
           "  background:#FFFFFF; color:#EF4444; border:1.5px solid #FECACA;"
           "  padding:7px 16px; border-radius:8px; font-weight:500; font-size:12px;"
           "}"
           "QPushButton:hover   { background:#FEF2F2; border-color:#EF4444; }"
           "QPushButton:pressed { background:#FEE2E2; }";
}
static QString tableStyle() {
    return "QTableWidget {"
           "  background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:10px;"
           "  gridline-color:#F3F4F6; font-size:12px; outline:0;"
           "}"
           "QTableWidget::item { padding:6px 12px; border:none; color:#374151; }"
           "QTableWidget::item:selected {"
           "  background:#F5EEFF; color:#6A1AB8;"
           "}"
           "QHeaderView::section {"
           "  background:#F9FAFB; color:#6B7280; border:none;"
           "  border-bottom:1.5px solid #E5E7EB; padding:8px 12px;"
           "  font-weight:600; font-size:11px; text-transform:uppercase;"
           "}"
           "QTableWidget QScrollBar:vertical {"
           "  background:#F9FAFB; width:8px; border-radius:4px;"
           "}"
           "QTableWidget QScrollBar::handle:vertical {"
           "  background:#D1D5DB; border-radius:4px; min-height:30px;"
           "}"
           "QTableWidget QScrollBar::handle:vertical:hover { background:#8222E3; }";
}

// Column indices
enum Col { ColStatus=0, ColName, ColSerial, ColDeviceIp, ColSocketIp, ColPort, ColLocation, ColLastSeen, ColCount };

DevicesPage::DevicesPage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    setStyleSheet("background:#F4F6FB;");

    // ── Page header ──────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setStyleSheet("background:#FFFFFF; border-bottom:1.5px solid #E5E7EB;");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);

    auto *titleBlock = new QVBoxLayout();
    auto *titleLabel = new QLabel("Devices");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700; color:#111827;");
    auto *subLabel = new QLabel("Manage ZKTeco attendance devices");
    subLabel->setStyleSheet("font-size:12px; color:#6B7280;");
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);

    auto *btnScan = new QPushButton("⊕  Scan Network");
    btnScan->setStyleSheet(primaryBtn());
    btnScan->setCursor(Qt::PointingHandCursor);

    auto *btnAdd = new QPushButton("+ Add Manually");
    btnAdd->setStyleSheet(secondaryBtn());
    btnAdd->setCursor(Qt::PointingHandCursor);

    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();
    hdrLayout->addWidget(btnScan);
    hdrLayout->addWidget(btnAdd);

    // ── Stat cards ───────────────────────────────────────────────────────────
    m_cardTotal  = makeStatCard("Total", "0",  "#8222E3");
    m_cardOnline = makeStatCard("Online", "0", "#10B981");
    m_cardOff    = makeStatCard("Offline", "0","#EF4444");

    auto *cardsRow = new QHBoxLayout();
    cardsRow->setSpacing(16);
    cardsRow->addWidget(m_cardTotal);
    cardsRow->addWidget(m_cardOnline);
    cardsRow->addWidget(m_cardOff);
    cardsRow->addStretch();

    // ── Table toolbar ────────────────────────────────────────────────────────
    m_btnEdit   = new QPushButton("✎  Edit");
    m_btnRemove = new QPushButton("✕  Remove");
    m_btnEdit->setStyleSheet(secondaryBtn());
    m_btnRemove->setStyleSheet(dangerBtn());
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
    m_table->setStyleSheet(tableStyle());
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

    refresh();
}

QWidget *DevicesPage::makeStatCard(const QString &label, const QString &value, const QString &color) {
    auto *card = new QWidget();
    card->setFixedSize(130, 68);
    card->setStyleSheet(QString(
        "background:#FFFFFF; border-radius:10px; border:1.5px solid #E5E7EB;"));

    auto *val = new QLabel(value);
    val->setObjectName("val");
    val->setStyleSheet(QString("font-size:22px; font-weight:700; color:%1;").arg(color));
    auto *lbl = new QLabel(label);
    lbl->setStyleSheet("font-size:11px; color:#6B7280; font-weight:500;");

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

void DevicesPage::populateTable(const QList<Device> &devices) {
    m_table->setRowCount(0);
    m_serialToRow.clear();

    for (int row = 0; row < devices.size(); ++row) {
        const Device &d = devices[row];
        m_table->insertRow(row);
        m_serialToRow[d.serialNumber] = row;

        bool online = m_onlineSet.contains(d.serialNumber);

        // Status pill
        auto *statusWidget = new QWidget();
        auto *statusLayout = new QHBoxLayout(statusWidget);
        statusLayout->setContentsMargins(8, 0, 8, 0);
        auto *pill = new QLabel(online ? "● Online" : "○ Offline");
        pill->setAlignment(Qt::AlignCenter);
        pill->setStyleSheet(online
            ? "background:#D1FAE5; color:#065F46; border-radius:10px; padding:3px 10px; font-size:11px; font-weight:600;"
            : "background:#F3F4F6; color:#6B7280; border-radius:10px; padding:3px 10px; font-size:11px; font-weight:600;");
        statusLayout->addWidget(pill, 0, Qt::AlignVCenter);
        m_table->setCellWidget(row, ColStatus, statusWidget);

        QColor textColor = d.isActive ? QColor("#374151") : QColor("#D1D5DB");
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

    auto *w = m_table->cellWidget(row, ColStatus);
    if (!w) return;
    auto *pill = w->findChild<QLabel*>();
    if (!pill) return;
    pill->setText(online ? "● Online" : "○ Offline");
    pill->setStyleSheet(online
        ? "background:#D1FAE5; color:#065F46; border-radius:10px; padding:3px 10px; font-size:11px; font-weight:600;"
        : "background:#F3F4F6; color:#6B7280; border-radius:10px; padding:3px 10px; font-size:11px; font-weight:600;");

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

    QMenu menu(this);
    menu.setStyleSheet(
        "QMenu { background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:8px; padding:4px; }"
        "QMenu::item { padding:8px 20px; border-radius:6px; font-size:12px; color:#374151; }"
        "QMenu::item:selected { background:#F5EEFF; color:#6A1AB8; }"
        "QMenu::separator { height:1px; background:#F3F4F6; margin:4px 8px; }");

    // Activate / Deactivate
    if (d.isActive) {
        auto *act = menu.addAction("⏸  Deactivate");
        connect(act, &QAction::triggered, this, [this, d]() {
            emit deactivateDeviceRequested(d.id);
        });
    } else {
        auto *act = menu.addAction("▶  Activate");
        connect(act, &QAction::triggered, this, [this, d]() {
            emit activateDeviceRequested(d.id);
        });
    }

    // Reconnect — only when device is currently connected
    if (online) {
        auto *act = menu.addAction("↺  Reconnect");
        connect(act, &QAction::triggered, this, [this, d]() {
            emit reconnectDeviceRequested(d.serialNumber);
        });
    }

    // Restart Device
    auto *actRestart = menu.addAction("⟳  Restart Device");
    connect(actRestart, &QAction::triggered, this, [this, d]() {
        emit restartDeviceRequested(d.serialNumber);
    });

    menu.addSeparator();

    // Biometric template operations
    auto *actBackup = menu.addAction("↑  Backup Biometric Templates");
    connect(actBackup, &QAction::triggered, this, [this, d]() {
        emit backupTemplatesRequested(d.serialNumber);
    });

    auto *actRestore = menu.addAction("↓  Restore Biometric Templates");
    connect(actRestore, &QAction::triggered, this, [this, d]() {
        emit restoreTemplatesRequested(d.serialNumber);
    });

    menu.addSeparator();

    auto *actEdit = menu.addAction("✎  Edit");
    connect(actEdit, &QAction::triggered, this, &DevicesPage::onEditDevice);

    auto *actDelete = menu.addAction("✕  Delete");
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
