#include "DeviceDiscoveryDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QFrame>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPropertyAnimation>

DeviceDiscoveryDialog::DeviceDiscoveryDialog(const QSet<QString> &existingSerials, QWidget *parent)
    : QDialog(parent), m_existingSerials(existingSerials)
{
    setWindowTitle("Discover Network Devices");
    setMinimumSize(640, 480);
    setStyleSheet(
        "QDialog { background:#F4F6FB; }"
        "QLabel  { color:#374151; }");

    // ── Title bar ─────────────────────────────────────────────────────────────
    auto *titleBar = new QWidget();
    titleBar->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #8222E3,stop:1 #A855F7);");
    titleBar->setFixedHeight(58);
    auto *tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(20, 0, 20, 0);
    auto *tbTitle = new QLabel("Discover ZKTeco Devices");
    tbTitle->setStyleSheet("color:#FFFFFF; font-size:16px; font-weight:700;");
    auto *tbSub = new QLabel("Automatically find devices on your network");
    tbSub->setStyleSheet("color:rgba(255,255,255,0.7); font-size:11px;");
    auto *tbTextCol = new QVBoxLayout();
    tbTextCol->setSpacing(2);
    tbTextCol->addWidget(tbTitle);
    tbTextCol->addWidget(tbSub);
    tbLayout->addLayout(tbTextCol);

    // ── Progress area ─────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Broadcasting discovery packet across your network...");
    m_statusLabel->setStyleSheet("font-size:12px; color:#6B7280;");

    m_progress = new QProgressBar();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(5);
    m_progress->setStyleSheet(
        "QProgressBar { background:#E5E7EB; border-radius:3px; border:none; }"
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "  stop:0 #8222E3, stop:1 #A855F7); border-radius:3px; }");

    // ── Table ─────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"", "Serial Number", "IP Address", "Model", "Status"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_table->setColumnWidth(0, 40);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setStyleSheet(
        "QTableWidget { background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:10px;"
        "  font-size:12px; outline:0; }"
        "QTableWidget::item { padding:6px 10px; border:none; color:#374151; }"
        "QHeaderView::section { background:#F9FAFB; color:#6B7280; border:none;"
        "  border-bottom:1.5px solid #E5E7EB; padding:8px 10px; font-weight:600; font-size:11px; }"
        "QScrollBar:vertical { background:#F9FAFB; width:8px; border-radius:4px; }"
        "QScrollBar::handle:vertical { background:#D1D5DB; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#8222E3; }");

    // ── Empty state label ─────────────────────────────────────────────────────
    m_emptyLabel = new QLabel("Searching for ZKTeco devices on your network...");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet("color:#9CA3AF; font-size:13px; padding:40px;");

    // ── Buttons ───────────────────────────────────────────────────────────────
    m_btnRescan = new QPushButton("⊕  Scan Again");
    m_btnRescan->setEnabled(false);
    m_btnRescan->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#374151; border:1.5px solid #E5E7EB;"
        "  padding:7px 16px; border-radius:8px; font-weight:500; font-size:12px; }"
        "QPushButton:enabled:hover { background:#F9F5FF; border-color:#8222E3; color:#8222E3; }"
        "QPushButton:disabled { color:#9CA3AF; }");
    m_btnRescan->setCursor(Qt::PointingHandCursor);

    m_btnAdd = new QPushButton("Add Selected Devices");
    m_btnAdd->setEnabled(false);
    m_btnAdd->setStyleSheet(
        "QPushButton { background:#8222E3; color:#FFFFFF; border:none;"
        "  padding:9px 24px; border-radius:8px; font-weight:700; font-size:13px; }"
        "QPushButton:enabled:hover   { background:#6A1AB8; }"
        "QPushButton:enabled:pressed { background:#5a14a0; }"
        "QPushButton:disabled { background:#D1D5DB; color:#9CA3AF; }");
    m_btnAdd->setCursor(Qt::PointingHandCursor);

    auto *btnClose = new QPushButton("Cancel");
    btnClose->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#374151; border:1.5px solid #E5E7EB;"
        "  padding:9px 20px; border-radius:8px; font-weight:500; font-size:12px; }"
        "QPushButton:hover { background:#F9F5FF; border-color:#8222E3; color:#8222E3; }");
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose,  &QPushButton::clicked, this, &QDialog::reject);
    connect(m_btnAdd,  &QPushButton::clicked, this, &DeviceDiscoveryDialog::onAddSelected);
    connect(m_btnRescan,&QPushButton::clicked,this, &DeviceDiscoveryDialog::onRescan);

    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(m_btnRescan);
    btnRow->addStretch();
    btnRow->addWidget(btnClose);
    btnRow->addWidget(m_btnAdd);

    // ── Body ─────────────────────────────────────────────────────────────────
    auto *body = new QVBoxLayout();
    body->setContentsMargins(24, 18, 24, 20);
    body->setSpacing(12);
    body->addWidget(m_progress);
    body->addWidget(m_statusLabel);
    body->addWidget(m_table);
    body->addLayout(btnRow);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(titleBar);
    root->addLayout(body);

    // ── Discovery ─────────────────────────────────────────────────────────────
    m_discovery = new DeviceDiscovery(this);
    connect(m_discovery, &DeviceDiscovery::deviceFound,  this, &DeviceDiscoveryDialog::onDeviceFound);
    connect(m_discovery, &DeviceDiscovery::scanFinished, this, &DeviceDiscoveryDialog::onScanFinished);
    connect(m_discovery, &DeviceDiscovery::progress,     this, &DeviceDiscoveryDialog::onProgress);
    m_discovery->startScan();
}

void DeviceDiscoveryDialog::onDeviceFound(DiscoveredDevice device) {
    if (m_seenSerials.contains(device.serialNumber)) return;
    m_seenSerials.insert(device.serialNumber);
    addDeviceRow(device);
    m_btnAdd->setEnabled(true);
}

void DeviceDiscoveryDialog::addDeviceRow(const DiscoveredDevice &dev) {
    int row = m_table->rowCount();
    m_table->insertRow(row);

    // Checkbox
    auto *chk = new QCheckBox();
    chk->setChecked(!m_existingSerials.contains(dev.serialNumber));
    chk->setEnabled(!m_existingSerials.contains(dev.serialNumber));
    chk->setStyleSheet(
        "QCheckBox::indicator { width:18px; height:18px; border-radius:5px;"
        "  border:1.5px solid #D1D5DB; background:#FFFFFF; }"
        "QCheckBox::indicator:checked { background:#8222E3; border-color:#8222E3; }");
    auto *chkCell = new QWidget();
    auto *chkCellLayout = new QHBoxLayout(chkCell);
    chkCellLayout->addWidget(chk);
    chkCellLayout->setAlignment(Qt::AlignCenter);
    chkCellLayout->setContentsMargins(0,0,0,0);
    m_table->setCellWidget(row, 0, chkCell);

    m_table->setItem(row, 1, new QTableWidgetItem(dev.serialNumber));
    m_table->setItem(row, 2, new QTableWidgetItem(dev.ipAddress));
    m_table->setItem(row, 3, new QTableWidgetItem(dev.model.isEmpty() ? "ZKTeco Device" : dev.model));

    // Status pill
    bool existing = m_existingSerials.contains(dev.serialNumber);
    auto *statusCell = new QWidget();
    auto *statusLayout = new QHBoxLayout(statusCell);
    statusLayout->setContentsMargins(6,0,6,0);
    auto *pill = new QLabel(existing ? "Already added" : "New");
    pill->setAlignment(Qt::AlignCenter);
    pill->setStyleSheet(existing
        ? "background:#D1FAE5; color:#065F46; border-radius:8px; padding:2px 10px; font-size:11px; font-weight:600;"
        : "background:#EDE5FB; color:#6A1AB8; border-radius:8px; padding:2px 10px; font-size:11px; font-weight:600;");
    statusLayout->addWidget(pill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 4, statusCell);
}

void DeviceDiscoveryDialog::onScanFinished(int total) {
    m_progress->setValue(100);
    m_btnRescan->setEnabled(true);
    if (total == 0) {
        m_statusLabel->setText(
            "No devices found. Ensure devices are on the same network with ADMS enabled.");
        m_statusLabel->setStyleSheet("font-size:12px; color:#F59E0B;");
        m_table->setRowCount(0);
        // Show empty-state hint row
        m_table->insertRow(0);
        auto *emptyItem = new QTableWidgetItem(
            "No devices responded. Try configuring device ADMS settings first.");
        emptyItem->setTextAlignment(Qt::AlignCenter);
        emptyItem->setForeground(QColor("#9CA3AF"));
        m_table->setItem(0, 1, emptyItem);
        m_table->setSpan(0, 1, 1, 4);
    } else {
        m_statusLabel->setText(QString("Found %1 device(s). Check devices to add and click \"Add Selected\".").arg(total));
        m_statusLabel->setStyleSheet("font-size:12px; color:#10B981; font-weight:600;");
    }
}

void DeviceDiscoveryDialog::onProgress(int percent, const QString &status) {
    m_progress->setValue(percent);
    m_statusLabel->setText(status);
    m_statusLabel->setStyleSheet("font-size:12px; color:#6B7280;");
    m_btnRescan->setEnabled(percent == 100);
}

void DeviceDiscoveryDialog::onRescan() {
    m_table->setRowCount(0);
    m_seenSerials.clear();
    m_btnAdd->setEnabled(false);
    m_progress->setValue(0);
    m_discovery->startScan();
}

void DeviceDiscoveryDialog::onAddSelected() {
    if (selectedDevices().isEmpty()) {
        QMessageBox::information(this, "Add Devices", "Please check at least one new device.");
        return;
    }
    accept();
}

QList<Device> DeviceDiscoveryDialog::selectedDevices() const {
    QList<Device> result;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        auto *chkCell = m_table->cellWidget(row, 0);
        auto *chk = chkCell ? chkCell->findChild<QCheckBox*>() : nullptr;
        if (!chk || !chk->isChecked() || !chk->isEnabled()) continue;
        auto *snItem = m_table->item(row, 1);
        auto *ipItem = m_table->item(row, 2);
        if (!snItem || !ipItem) continue;
        Device d;
        d.name         = QString("Device %1").arg(snItem->text());
        d.serialNumber = snItem->text();
        d.ipAddress    = ipItem->text();
        d.port         = 4370;
        result.append(d);
    }
    return result;
}
