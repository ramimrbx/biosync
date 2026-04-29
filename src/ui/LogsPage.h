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

private slots:
    void loadLogs();

private:
    Database       *m_db;
    QPlainTextEdit *m_logView;
    QComboBox      *m_deviceFilter;
};
