#include "AttendancePage.h"
#include "Theme.h"
#include "Icons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QAction>

AttendancePage::AttendancePage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    // ── Header ────────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setObjectName("attHeader");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);

    auto *titleLabel = new QLabel("Attendance");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700;");
    m_statsLabel = new QLabel("Loading...");
    m_statsLabel->setStyleSheet("font-size:12px; color:palette(mid);");
    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(m_statsLabel);

    auto *btnRefresh = new QPushButton(" Refresh");
    btnRefresh->setIcon(Icons::icon(Icons::Glyph::Refresh, Theme::current().textPrimary, 14, devicePixelRatioF()));
    btnRefresh->setCursor(Qt::PointingHandCursor);
    connect(btnRefresh, &QPushButton::clicked, this, &AttendancePage::refresh);

    m_btnRetry = new QPushButton(" Retry Failed");
    m_btnRetry->setIcon(Icons::icon(Icons::Glyph::Retry, Theme::current().danger, 14, devicePixelRatioF()));
    m_btnRetry->setCursor(Qt::PointingHandCursor);
    m_btnRetry->setToolTip("No failed records to retry");
    m_btnRetry->setEnabled(false);
    connect(m_btnRetry, &QPushButton::clicked, this, &AttendancePage::retryFailedRequested);

    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();
    hdrLayout->addWidget(btnRefresh);
    hdrLayout->addWidget(m_btnRetry);

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
    m_table->setColumnWidth(5, 100);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->hide();
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(44);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &AttendancePage::onContextMenuRequested);

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

    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &AttendancePage::applyTheme);

    refresh();
}

void AttendancePage::applyTheme() {
    const ThemePalette &t = Theme::current();
    setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));
    findChild<QWidget*>("attHeader")->setStyleSheet(
        QString("#attHeader{background:%1; border-bottom:1.5px solid %2;}")
            .arg(t.headerBg.name(), t.border.name()));
    populateTable(m_db->getRecentRecords(m_table->rowCount() > 0 ? m_table->rowCount() : 500));
}

void AttendancePage::refresh() {
    auto records = m_db->getRecentRecords(500);
    populateTable(records);
    int pending = m_db->getPendingCount();
    m_statsLabel->setText(QString("%1 records  ·  %2 pending sync")
                          .arg(records.size()).arg(pending));

    const int failed = m_db->getFailedCount();
    m_btnRetry->setEnabled(failed > 0);
    m_btnRetry->setToolTip(failed > 0
        ? QString("Retry %1 failed record(s) now").arg(failed)
        : "No failed records to retry");
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
    const ThemePalette &t = Theme::current();
    auto item = [](const QString &text) { return new QTableWidgetItem(text); };

    auto *deviceItem = item(r.deviceSerial);
    deviceItem->setData(Qt::UserRole, r.id);
    deviceItem->setData(Qt::UserRole + 1, static_cast<int>(r.syncStatus));
    m_table->setItem(row, 0, deviceItem);
    m_table->setItem(row, 1, item(r.userPin));
    m_table->setItem(row, 2, item(r.punchTime.toString("yyyy-MM-dd  hh:mm:ss")));

    // Direction pill (cell widget) — an opaque background of its own so a selected row's
    // highlight tint never peeks through the layout margins around the pill.
    bool isIn = (r.punchStatus == 0 || r.punchStatus == 3 || r.punchStatus == 4);
    auto *dirWidget = new QWidget();
    dirWidget->setAttribute(Qt::WA_StyledBackground, true);
    dirWidget->setStyleSheet(QString("background:%1;").arg(t.cardBg.name()));
    auto *dirLayout = new QHBoxLayout(dirWidget);
    dirLayout->setContentsMargins(8, 0, 8, 0);
    auto *pill = new QLabel(r.punchStatusName());
    pill->setAlignment(Qt::AlignCenter);
    QColor pillFg = isIn ? t.success : t.danger;
    QColor pillBg = isIn ? t.successBg : t.dangerBg;
    pill->setStyleSheet(QString("background:%1; color:%2; border-radius:8px; padding:2px 10px; "
                                 "font-size:11px; font-weight:600;")
                            .arg(pillBg.name(), pillFg.name()));
    dirLayout->addWidget(pill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 3, dirWidget);

    m_table->setItem(row, 4, item(r.verifyTypeName()));

    // Sync status pill, with a matching status icon
    auto *syncWidget = new QWidget();
    auto *syncLayout = new QHBoxLayout(syncWidget);
    syncLayout->setContentsMargins(8, 0, 8, 0);
    syncLayout->setSpacing(5);

    Icons::Glyph glyph = Icons::Glyph::Pending;
    QColor fg = t.warning, bg = t.warningBg;
    if (r.syncStatus == SyncStatus::Synced) { glyph = Icons::Glyph::Success; fg = t.success; bg = t.successBg; }
    else if (r.syncStatus == SyncStatus::Failed) { glyph = Icons::Glyph::Error; fg = t.danger; bg = t.dangerBg; }

    auto *iconLbl = new QLabel();
    iconLbl->setPixmap(Icons::pixmap(glyph, fg, 13, devicePixelRatioF()));
    auto *syncPill = new QLabel(r.syncStatusName());
    syncPill->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;").arg(fg.name()));
    syncWidget->setStyleSheet(QString("background:%1; border-radius:8px;").arg(bg.name()));
    syncLayout->addWidget(iconLbl, 0, Qt::AlignVCenter);
    syncLayout->addWidget(syncPill, 0, Qt::AlignVCenter);
    m_table->setCellWidget(row, 5, syncWidget);

    m_table->setItem(row, 6, item(r.workCode.isEmpty() ? "—" : r.workCode));
    m_table->setItem(row, 7, item(r.deviceName.isEmpty() ? "—" : r.deviceName));
}

void AttendancePage::onContextMenuRequested(const QPoint &pos) {
    int row = m_table->rowAt(pos.y());
    if (row < 0) return;
    auto *idItem = m_table->item(row, 0);
    if (!idItem) return;

    const int recordId = idItem->data(Qt::UserRole).toInt();
    const auto syncStatus = static_cast<SyncStatus>(idItem->data(Qt::UserRole + 1).toInt());
    if (recordId == 0 || syncStatus != SyncStatus::Failed) return; // retry only offered on failed rows

    m_table->selectRow(row);
    QMenu menu(this);
    auto *actRetry = menu.addAction(Icons::icon(Icons::Glyph::Retry, Theme::current().textPrimary, 16,
                                                 devicePixelRatioF()),
                                     "Retry this record");
    connect(actRetry, &QAction::triggered, this, [this, recordId]() { emit retryOneRequested(recordId); });
    menu.exec(m_table->viewport()->mapToGlobal(pos));
}
