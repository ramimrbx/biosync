#include "DeviceDiscoveryDialog.h"
#include "ColumnWidths.h"
#include "ui/Theme.h"
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
    const ThemePalette &t = Theme::current();
    setWindowTitle("Discover Network Devices");
    setMinimumSize(640, 480);
    setStyleSheet(QString(
        "QDialog { background:%1; }"
        "QLabel  { color:%2; }").arg(t.pageBg.name(), t.textSecondary.name()));

    // ── Header ───────────────────────────────────────────────────────────────
    // Mirrors ritems' Modal.tsx "default" header: a soft accent wash with an accent-colored
    // title, not a solid brand-color banner.
    auto *titleBar = new QWidget();
    titleBar->setAttribute(Qt::WA_StyledBackground, true);
    titleBar->setStyleSheet(QString(
        "background:%1; border-bottom:1px solid %2;").arg(t.accentSoft(), t.border.name()));
    titleBar->setFixedHeight(58);
    auto *tbLayout = new QHBoxLayout(titleBar);
    tbLayout->setContentsMargins(20, 0, 20, 0);
    auto *tbTitle = new QLabel("Discover Devices");
    tbTitle->setStyleSheet(QString("color:%1; font-size:16px; font-weight:700;").arg(t.accent.name()));
    auto *tbSub = new QLabel("Automatically find devices on your network");
    tbSub->setStyleSheet(QString("color:%1; font-size:11px;").arg(t.textSecondary.name()));
    auto *tbTextCol = new QVBoxLayout();
    tbTextCol->setSpacing(2);
    tbTextCol->addWidget(tbTitle);
    tbTextCol->addWidget(tbSub);
    tbLayout->addLayout(tbTextCol);

    // ── Progress area ─────────────────────────────────────────────────────────
    m_statusLabel = new QLabel("Broadcasting discovery packet across your network...");
    m_statusLabel->setStyleSheet(QString("font-size:12px; color:%1;").arg(t.textSecondary.name()));

    m_progress = new QProgressBar();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(5);
    m_progress->setStyleSheet(QString(
        "QProgressBar { background:%1; border-radius:3px; border:none; }"
        "QProgressBar::chunk { background:%2; border-radius:3px; }")
        .arg(t.border.name(), t.accent.name()));

    // ── Table ─────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 5);
    m_table->setHorizontalHeaderLabels({"", "Serial Number", "IP Address", "Model", "Status"});
    // Check box, Serial Number, IP Address, Model (Status fills the rest)
    ColumnWidths::makeResizable(m_table, "discovery", {40, 160, 130, 150});
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setStyleSheet(QString(
        "QTableWidget { background:%1; border:1.5px solid %2; border-radius:12px;"
        "  font-size:13px; outline:0; }"
        "QTableWidget::item { padding:6px 10px; border:none; color:%3; }"
        "QHeaderView::section { background:%4; color:%5; border:none;"
        "  border-bottom:1.5px solid %2; padding:10px 10px; font-weight:700; font-size:11px; }"
        "QScrollBar:vertical { background:%4; width:8px; border-radius:4px; }"
        "QScrollBar::handle:vertical { background:%6; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:%7; }")
        .arg(t.cardBg.name(), t.border.name(), t.textSecondary.name(), t.headerBg.name(),
             t.textDisabled.name(), t.scrollHandle.name(), t.accent.name()));

    // ── Empty state label ─────────────────────────────────────────────────────
    m_emptyLabel = new QLabel("Searching for ZKTeco and RitOS devices on your network...");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QString("color:%1; font-size:13px; padding:40px;").arg(t.placeholder.name()));

    // ── Buttons ───────────────────────────────────────────────────────────────
    m_btnRescan = new QPushButton("⊕  Scan Again");
    m_btnRescan->setEnabled(false);
    m_btnRescan->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:1.5px solid %3;"
        "  padding:8px 16px; border-radius:12px; font-weight:600; font-size:12px; }"
        "QPushButton:enabled:hover { background:%4; border-color:%5; color:%5; }"
        "QPushButton:disabled { color:%6; }")
        .arg(t.cardBg.name(), t.textSecondary.name(), t.border.name(), t.accentSoft(),
             t.accent.name(), t.textDisabled.name()));
    m_btnRescan->setCursor(Qt::PointingHandCursor);

    m_btnAdd = new QPushButton("Add Selected Devices");
    m_btnAdd->setEnabled(false);
    m_btnAdd->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:none;"
        "  padding:10px 24px; border-radius:12px; font-weight:700; font-size:13px; }"
        "QPushButton:enabled:hover   { background:%3; }"
        "QPushButton:enabled:pressed { background:%4; }"
        "QPushButton:disabled { background:%5; color:%6; }")
        .arg(t.accent.name(), t.onAccent.name(), t.accentHover.name(), t.accentPressed.name(),
             t.border.name(), t.textDisabled.name()));
    m_btnAdd->setCursor(Qt::PointingHandCursor);

    auto *btnClose = new QPushButton("Cancel");
    btnClose->setStyleSheet(QString(
        "QPushButton { background:%1; color:%2; border:1.5px solid %3;"
        "  padding:10px 20px; border-radius:12px; font-weight:600; font-size:12px; }"
        "QPushButton:hover { background:%4; border-color:%5; color:%5; }")
        .arg(t.cardBg.name(), t.textSecondary.name(), t.border.name(), t.accentSoft(), t.accent.name()));
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

    const ThemePalette &t = Theme::current();

    // Checkbox
    auto *chk = new QCheckBox();
    chk->setChecked(!m_existingSerials.contains(dev.serialNumber));
    chk->setEnabled(!m_existingSerials.contains(dev.serialNumber));
    chk->setStyleSheet(QString(
        "QCheckBox::indicator { width:18px; height:18px; border-radius:5px;"
        "  border:1.5px solid %1; background:%2; }"
        "QCheckBox::indicator:checked { background:%3; border-color:%3; }")
        .arg(t.border.name(), t.cardBg.name(), t.accent.name()));
    auto *chkCell = new QWidget();
    auto *chkCellLayout = new QHBoxLayout(chkCell);
    chkCellLayout->addWidget(chk);
    chkCellLayout->setAlignment(Qt::AlignCenter);
    chkCellLayout->setContentsMargins(0,0,0,0);
    m_table->setCellWidget(row, 0, chkCell);

    m_table->setItem(row, 1, new QTableWidgetItem(dev.serialNumber));
    m_table->setItem(row, 2, new QTableWidgetItem(dev.ipAddress));
    m_table->setItem(row, 3, new QTableWidgetItem(dev.model.isEmpty() ? "ZKTeco Device" : dev.model));

    // Status pill — matches ritems' Badge.tsx: success (already added) vs. primary (new).
    bool existing = m_existingSerials.contains(dev.serialNumber);
    auto *statusCell = new QWidget();
    auto *statusLayout = new QHBoxLayout(statusCell);
    statusLayout->setContentsMargins(6,0,6,0);
    auto *pill = new QLabel(existing ? "Already added" : "New");
    pill->setAlignment(Qt::AlignCenter);
    const QColor &sb = t.successBg;
    pill->setStyleSheet(existing
        ? QString("background:rgba(%1,%2,%3,%4); color:%5; border-radius:9px; padding:2px 10px; font-size:11px; font-weight:600;")
              .arg(sb.red()).arg(sb.green()).arg(sb.blue()).arg(sb.alpha()).arg(t.success.name())
        : QString("background:%1; color:%2; border-radius:9px; padding:2px 10px; font-size:11px; font-weight:600;")
              .arg(t.accentSoft(), t.accent.name()));
    statusLayout->addWidget(pill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 4, statusCell);
}

void DeviceDiscoveryDialog::onScanFinished(int total) {
    const ThemePalette &t = Theme::current();
    m_progress->setValue(100);
    m_btnRescan->setEnabled(true);
    if (total == 0) {
        m_statusLabel->setText(
            "No devices found. Ensure devices are on the same network with ADMS (ZKTeco) or Send to server (RitOS) enabled.");
        m_statusLabel->setStyleSheet(QString("font-size:12px; color:%1;").arg(t.warning.name()));
        m_table->setRowCount(0);
        // Show empty-state hint row
        m_table->insertRow(0);
        auto *emptyItem = new QTableWidgetItem(
            "No devices responded. Try configuring the device server settings first.");
        emptyItem->setTextAlignment(Qt::AlignCenter);
        emptyItem->setForeground(t.placeholder);
        m_table->setItem(0, 1, emptyItem);
        m_table->setSpan(0, 1, 1, 4);
    } else {
        m_statusLabel->setText(QString("Found %1 device(s). Check devices to add and click \"Add Selected\".").arg(total));
        m_statusLabel->setStyleSheet(QString("font-size:12px; color:%1; font-weight:600;").arg(t.success.name()));
    }
}

void DeviceDiscoveryDialog::onProgress(int percent, const QString &status) {
    m_progress->setValue(percent);
    m_statusLabel->setText(status);
    m_statusLabel->setStyleSheet(QString("font-size:12px; color:%1;").arg(Theme::current().textSecondary.name()));
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
