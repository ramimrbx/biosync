#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include "../db/Database.h"

class AttendancePage : public QWidget {
    Q_OBJECT
public:
    explicit AttendancePage(Database *db, QWidget *parent = nullptr);

    void refresh();
    void prependRecord(const AttendanceRecord &record);

private:
    Database     *m_db;
    QTableWidget *m_table;
    QLabel       *m_statsLabel;

    void populateTable(const QList<AttendanceRecord> &records);
    void addRowToTable(int row, const AttendanceRecord &r);
};
