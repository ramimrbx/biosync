#include "AttendancePage.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>

static QString tableStyle() {
    return "QTableWidget {"
           "  background:#FFFFFF; border:1.5px solid #E5E7EB; border-radius:10px;"
           "  gridline-color:#F9FAFB; font-size:12px; outline:0;"
           "}"
           "QTableWidget::item { padding:6px 12px; border:none; color:#374151; }"
           "QTableWidget::item:selected { background:#F5EEFF; color:#6A1AB8; }"
           "QHeaderView::section {"
           "  background:#F9FAFB; color:#6B7280; border:none;"
           "  border-bottom:1.5px solid #E5E7EB; padding:8px 12px;"
           "  font-weight:600; font-size:11px;"
           "}"
           "QScrollBar:vertical { background:#F9FAFB; width:8px; border-radius:4px; }"
           "QScrollBar::handle:vertical { background:#D1D5DB; border-radius:4px; min-height:30px; }"
           "QScrollBar::handle:vertical:hover { background:#8222E3; }";
}

AttendancePage::AttendancePage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    setStyleSheet("background:#F4F6FB;");

    // ── Header ────────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setStyleSheet("background:#FFFFFF; border-bottom:1.5px solid #E5E7EB;");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);

    auto *titleLabel = new QLabel("Attendance");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700; color:#111827;");
    m_statsLabel = new QLabel("Loading...");
    m_statsLabel->setStyleSheet("font-size:12px; color:#6B7280;");
    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(m_statsLabel);

    auto *btnRefresh = new QPushButton("↻  Refresh");
    btnRefresh->setStyleSheet(
        "QPushButton { background:#FFFFFF; color:#374151; border:1.5px solid #E5E7EB;"
        "  padding:7px 16px; border-radius:8px; font-weight:500; font-size:12px; }"
        "QPushButton:hover { background:#F9F5FF; border-color:#8222E3; color:#8222E3; }");
    btnRefresh->setCursor(Qt::PointingHandCursor);
    connect(btnRefresh, &QPushButton::clicked, this, &AttendancePage::refresh);

    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();
    hdrLayout->addWidget(btnRefresh);

    // ── Table ─────────────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, 8);
    m_table->setHorizontalHeaderLabels(
        {"Device", "User PIN", "Punch Time", "Direction", "Verify", "Sync", "Work Code", "Device Name"});
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
    m_table->setColumnWidth(3, 90);
    m_table->setColumnWidth(4, 90);
    m_table->setColumnWidth(5, 90);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setStyleSheet(tableStyle());
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(44);

    // ── Body ─────────────────────────────────────────────────────────────────
    auto *body = new QVBoxLayout();
    body->setContentsMargins(28, 20, 28, 20);
    body->setSpacing(16);
    body->addWidget(m_table);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addLayout(body);

    refresh();
}

void AttendancePage::refresh() {
    auto records = m_db->getRecentRecords(500);
    populateTable(records);
    int pending = m_db->getPendingCount();
    m_statsLabel->setText(QString("%1 records  ·  %2 pending sync")
                          .arg(records.size()).arg(pending));
}

void AttendancePage::prependRecord(const AttendanceRecord &record) {
    m_table->setSortingEnabled(false);
    m_table->insertRow(0);
    addRowToTable(0, record);
    m_table->setSortingEnabled(true);
    int pending = m_db->getPendingCount();
    m_statsLabel->setText(QString("%1 records  ·  %2 pending sync")
                          .arg(m_table->rowCount()).arg(pending));
}

void AttendancePage::populateTable(const QList<AttendanceRecord> &records) {
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    for (int i = 0; i < records.size(); ++i) {
        m_table->insertRow(i);
        addRowToTable(i, records[i]);
    }
    m_table->setSortingEnabled(true);
}

void AttendancePage::addRowToTable(int row, const AttendanceRecord &r) {
    auto item = [](const QString &text) { return new QTableWidgetItem(text); };

    m_table->setItem(row, 0, item(r.deviceSerial));
    m_table->setItem(row, 1, item(r.userPin));
    m_table->setItem(row, 2, item(r.punchTime.toString("yyyy-MM-dd  hh:mm:ss")));

    // Direction pill (cell widget)
    bool isIn = (r.punchStatus == 0 || r.punchStatus == 3 || r.punchStatus == 4);
    auto *dirWidget = new QWidget();
    auto *dirLayout = new QHBoxLayout(dirWidget);
    dirLayout->setContentsMargins(8, 0, 8, 0);
    auto *pill = new QLabel(r.punchStatusName());
    pill->setAlignment(Qt::AlignCenter);
    pill->setStyleSheet(isIn
        ? "background:#D1FAE5; color:#065F46; border-radius:8px; padding:2px 10px; font-size:11px; font-weight:600;"
        : "background:#FEE2E2; color:#991B1B; border-radius:8px; padding:2px 10px; font-size:11px; font-weight:600;");
    dirLayout->addWidget(pill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 3, dirWidget);

    m_table->setItem(row, 4, item(r.verifyTypeName()));

    // Sync status pill
    auto *syncWidget = new QWidget();
    auto *syncLayout = new QHBoxLayout(syncWidget);
    syncLayout->setContentsMargins(8, 0, 8, 0);
    auto *syncPill = new QLabel(r.syncStatusName());
    syncPill->setAlignment(Qt::AlignCenter);
    QString syncStyle;
    if (r.syncStatus == SyncStatus::Synced)
        syncStyle = "background:#D1FAE5; color:#065F46;";
    else if (r.syncStatus == SyncStatus::Failed)
        syncStyle = "background:#FEE2E2; color:#991B1B;";
    else
        syncStyle = "background:#FEF3C7; color:#92400E;";
    syncPill->setStyleSheet(syncStyle + "border-radius:8px; padding:2px 10px; font-size:11px; font-weight:600;");
    syncLayout->addWidget(syncPill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 5, syncWidget);

    m_table->setItem(row, 6, item(r.workCode.isEmpty() ? "—" : r.workCode));
    m_table->setItem(row, 7, item(r.deviceName.isEmpty() ? "—" : r.deviceName));
}
