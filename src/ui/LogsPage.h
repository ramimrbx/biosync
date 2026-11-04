#pragma once
#include <QWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include "../db/Database.h"

class LogsPage : public QWidget {
    Q_OBJECT
public:
    explicit LogsPage(Database *db, QWidget *parent = nullptr);

    void appendLog(const QString &deviceSerial, const QString &message);
    void refreshDeviceFilter();
    // Append log lines written to the shared DB since the last load/tail (by this window or by
    // the background service).
    void tail();

private slots:
    void loadLogs();
    void applyTheme();

private:
    void appendStoredLine(const QString &line);

    Database       *m_db;
    int             m_lastLogId = 0;
    QPlainTextEdit *m_logView;
    QComboBox      *m_deviceFilter;
};
