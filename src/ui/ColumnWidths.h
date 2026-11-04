#pragma once
#include <QList>
#include <QString>

class QTableWidget;

// Spreadsheet-style column widths for a table: drag the divider between two column titles to
// resize, double-click it to fit the contents, right-click the titles to reset. Widths are saved
// per user under `key` and restored the next time, so they never reset by themselves. The last
// column fills whatever space is left.
namespace ColumnWidths {
void makeResizable(QTableWidget *table, const QString &key, const QList<int> &defaultWidths);
}
