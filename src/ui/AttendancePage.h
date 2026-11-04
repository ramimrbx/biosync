#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include "../db/Database.h"

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(Database *db, QWidget *parent = nullptr);

    void refresh();
    void prependRecord(const AttendanceRecord &record);

signals:
    void retryFailedRequested();          // retry every currently-failed record
    void retryOneRequested(int recordId); // retry a single record (right-click on a failed row)

private slots:
    void onContextMenuRequested(const QPoint &pos);

private:
    Database     *m_db;
    QTableWidget *m_table;
    QLabel       *m_statsLabel;
    QPushButton  *m_btnRetry;

    void populateTable(const QList<AttendanceRecord> &records);
    void addRowToTable(int row, const AttendanceRecord &r);
    void applyTheme();
};
