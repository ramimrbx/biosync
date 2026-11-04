#include "LogsPage.h"
#include "Theme.h"
#include "Icons.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QFont>
#include <QTextCursor>

LogsPage::LogsPage(Database *db, QWidget *parent)
    : QWidget(parent), m_db(db)
{
    // ── Header ────────────────────────────────────────────────────────────────
    auto *header = new QWidget();
    header->setObjectName("logsHeader");
    header->setFixedHeight(70);
    auto *hdrLayout = new QHBoxLayout(header);
    hdrLayout->setContentsMargins(28, 0, 28, 0);

    auto *titleLabel = new QLabel("Device Logs");
    titleLabel->setStyleSheet("font-size:18px; font-weight:700;");
    auto *subLabel = new QLabel("Real-time communication log");
    subLabel->setStyleSheet("font-size:12px; color:palette(mid);");
    auto *titleBlock = new QVBoxLayout();
    titleBlock->setSpacing(2);
    titleBlock->addWidget(titleLabel);
    titleBlock->addWidget(subLabel);

    m_deviceFilter = new QComboBox();
    m_deviceFilter->addItem("All Devices", "");

    auto *btnRefresh = new QPushButton(" Refresh");
    auto *btnClear   = new QPushButton("Clear");
    btnRefresh->setIcon(Icons::icon(Icons::Glyph::Refresh, Theme::current().textPrimary, 14, devicePixelRatioF()));
    btnRefresh->setCursor(Qt::PointingHandCursor);
    btnClear->setCursor(Qt::PointingHandCursor);

    hdrLayout->addLayout(titleBlock);
    hdrLayout->addStretch();
    hdrLayout->addWidget(new QLabel("Filter:"));
    hdrLayout->addWidget(m_deviceFilter);
    hdrLayout->addWidget(btnRefresh);
    hdrLayout->addWidget(btnClear);

    // ── Log viewer (dark terminal) ────────────────────────────────────────────
    m_logView = new QPlainTextEdit();
    m_logView->setReadOnly(true);
    m_logView->setFont(QFont("Consolas,Courier New,monospace", 11));
    m_logView->setStyleSheet(
        "QPlainTextEdit {"
        "  background:#0F0A1E; color:#C8B8F0;"
        "  border:1.5px solid #2D1A4A; border-radius:10px;"
        "  padding:12px; selection-background-color:#3D1F7A;"
        "}"
        "QScrollBar:vertical { background:#1A0D33; width:8px; border-radius:4px; }"
        "QScrollBar::handle:vertical { background:#4A2B8C; border-radius:4px; min-height:30px; }"
        "QScrollBar::handle:vertical:hover { background:#8222E3; }");
    m_logView->setMaximumBlockCount(2000);
    m_logView->setPlaceholderText("  Waiting for device connections...");

    // ── Legend row ────────────────────────────────────────────────────────────
    auto *legend = new QHBoxLayout();
    legend->setSpacing(20);
    auto mkLegend = [](const QString &color, const QString &text) {
        auto *w = new QWidget();
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0,0,0,0); l->setSpacing(6);
        auto *dot = new QLabel("●");
        dot->setStyleSheet(QString("color:%1; font-size:10px;").arg(color));
        auto *lbl = new QLabel(text);
        lbl->setStyleSheet("font-size:11px; color:#6B7280;");
        l->addWidget(dot); l->addWidget(lbl);
        return w;
    };
    legend->addWidget(mkLegend("#10B981", "Heartbeat"));
    legend->addWidget(mkLegend("#8222E3", "Attendance"));
    legend->addWidget(mkLegend("#F59E0B", "Warning"));
    legend->addWidget(mkLegend("#EF4444", "Error"));
    legend->addStretch();

    // ── Body ─────────────────────────────────────────────────────────────────
    auto *body = new QVBoxLayout();
    body->setContentsMargins(28, 20, 28, 20);
    body->setSpacing(12);
    body->addLayout(legend);
    body->addWidget(m_logView);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(header);
    root->addLayout(body);

    connect(m_deviceFilter, &QComboBox::currentIndexChanged, this, &LogsPage::loadLogs);
    connect(btnRefresh,     &QPushButton::clicked,           this, &LogsPage::loadLogs);
    connect(btnClear,       &QPushButton::clicked,           m_logView, &QPlainTextEdit::clear);

    applyTheme();
    connect(&Theme::instance(), &Theme::changed, this, &LogsPage::applyTheme);

    loadLogs();
}

void LogsPage::applyTheme() {
    const ThemePalette &t = Theme::current();
    setStyleSheet(QString("background:%1;").arg(t.pageBg.name()));
    findChild<QWidget*>("logsHeader")->setStyleSheet(
        QString("#logsHeader{background:%1; border-bottom:1.5px solid %2;}")
            .arg(t.headerBg.name(), t.border.name()));
    // The log viewer keeps its own permanently-dark "terminal" look regardless of app theme.
}

void LogsPage::appendLog(const QString &deviceSerial, const QString &message) {
    QString ts     = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString prefix = deviceSerial.isEmpty() ? "SYSTEM" : deviceSerial;

    // Choose colour based on content
    QString color = "#C8B8F0"; // default purple-tint
    QString lower = message.toLower();
    if (lower.contains("heartbeat") || lower.contains("online"))
        color = "#6EE7B7";
    else if (lower.contains("attendance") || lower.contains("record"))
        color = "#C084FC";
    else if (lower.contains("error") || lower.contains("fail"))
        color = "#FCA5A5";
    else if (lower.contains("warn") || lower.contains("offline"))
        color = "#FCD34D";

    // Build HTML line
    QString line = QString(
        "<span style='color:#4A3A6A;'>%1</span> "
        "<span style='color:#8222E3; font-weight:600;'>[%2]</span> "
        "<span style='color:%3;'>%4</span>")
        .arg(ts, prefix, color, message.toHtmlEscaped());

    m_logView->appendHtml(line);

    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logView->setTextCursor(cursor);
}

void LogsPage::refreshDeviceFilter() {
    QString current = m_deviceFilter->currentData().toString();
    m_deviceFilter->blockSignals(true);
    m_deviceFilter->clear();
    m_deviceFilter->addItem("All Devices", "");
    for (const auto &d : m_db->getAllDevices())
        m_deviceFilter->addItem(QString("%1  (%2)").arg(d.name, d.serialNumber), d.serialNumber);
    int idx = m_deviceFilter->findData(current);
    m_deviceFilter->setCurrentIndex(idx >= 0 ? idx : 0);
    m_deviceFilter->blockSignals(false);
}

void LogsPage::appendStoredLine(const QString &line) {
    // Re-colorise stored plain-text lines
    QStringList parts = line.split(" | ");
    if (parts.size() >= 3) {
        QString ts  = parts[0].trimmed();
        QString src = parts[1].trimmed();
        QString msg = parts.mid(2).join(" | ");
        QString color = "#C8B8F0";
        QString lower = msg.toLower();
        if (lower.contains("heartbeat") || lower.contains("online"))  color = "#6EE7B7";
        else if (lower.contains("attendance") || lower.contains("record")) color = "#C084FC";
        else if (lower.contains("error") || lower.contains("fail"))   color = "#FCA5A5";
        else if (lower.contains("warn") || lower.contains("offline")) color = "#FCD34D";

        m_logView->appendHtml(QString(
            "<span style='color:#4A3A6A;'>%1</span> "
            "<span style='color:#8222E3; font-weight:600;'>[%2]</span> "
            "<span style='color:%3;'>%4</span>")
            .arg(ts, src, color, msg.toHtmlEscaped()));
    } else {
        m_logView->appendHtml(QString("<span style='color:#C8B8F0;'>%1</span>")
                              .arg(line.toHtmlEscaped()));
    }
}

void LogsPage::loadLogs() {
    QString serial = m_deviceFilter->currentData().toString();
    m_lastLogId = m_db->maxLogId();
    QStringList logs = m_db->getDeviceLogs(serial, 500);
    m_logView->clear();
    for (const QString &line : logs) appendStoredLine(line);
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logView->setTextCursor(cursor);
}

void LogsPage::tail() {
    const QStringList lines = m_db->getLogsAfter(m_lastLogId, m_deviceFilter->currentData().toString());
    if (lines.isEmpty()) return;
    for (const QString &line : lines) appendStoredLine(line);
    QTextCursor cursor = m_logView->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logView->setTextCursor(cursor);
}
