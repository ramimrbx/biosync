#include "ColumnWidths.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QSettings>
#include <QTimer>
#include <QMenu>
#include <QPointer>

namespace {
constexpr int MIN_WIDTH = 40;
constexpr int SAVE_DELAY_MS = 400;   // save once a drag settles, not on every pixel

QSettings prefs() { return QSettings(QStringLiteral("Right iTech"), QStringLiteral("BioSync")); }
QString settingsKey(const QString &key) { return QStringLiteral("ui/columns/") + key; }

void applyWidths(QTableWidget *table, const QList<int> &widths) {
    QHeaderView *header = table->horizontalHeader();
    // The last column stretches to fill the table, so its width is never set or saved.
    for (int i = 0; i < table->columnCount() - 1 && i < widths.size(); ++i)
        header->resizeSection(i, qMax(MIN_WIDTH, widths[i]));
}

QList<int> savedWidths(const QString &key, int columns) {
    QList<int> widths;
    const QStringList saved = prefs().value(settingsKey(key)).toStringList();
    if (saved.size() != columns - 1) return widths;   // layout changed in a newer version
    for (const QString &w : saved) widths << w.toInt();
    return widths;
}
}

void ColumnWidths::makeResizable(QTableWidget *table, const QString &key, const QList<int> &defaultWidths) {
    QHeaderView *header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(true);
    header->setMinimumSectionSize(MIN_WIDTH);
    header->setSectionsClickable(true);

    const QList<int> saved = savedWidths(key, table->columnCount());
    applyWidths(table, saved.isEmpty() ? defaultWidths : saved);

    auto *saveTimer = new QTimer(table);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(SAVE_DELAY_MS);
    QPointer<QTableWidget> guard(table);
    QObject::connect(saveTimer, &QTimer::timeout, table, [guard, key]() {
        if (!guard) return;
        QStringList widths;
        for (int i = 0; i < guard->columnCount() - 1; ++i)
            widths << QString::number(guard->horizontalHeader()->sectionSize(i));
        QSettings s = prefs();
        s.setValue(settingsKey(key), widths);
    });
    QObject::connect(header, &QHeaderView::sectionResized, table,
                     [table, saveTimer](int index, int, int) {
                         if (index < table->columnCount() - 1) saveTimer->start();
                     });

    header->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(header, &QHeaderView::customContextMenuRequested, table,
                     [table, header, key, defaultWidths](const QPoint &pos) {
                         QMenu menu(table);
                         QAction *fit   = menu.addAction("Fit all columns to contents");
                         QAction *reset = menu.addAction("Reset column widths");
                         QAction *chosen = menu.exec(header->mapToGlobal(pos));
                         if (chosen == fit) {
                             for (int i = 0; i < table->columnCount() - 1; ++i) table->resizeColumnToContents(i);
                         } else if (chosen == reset) {
                             QSettings s = prefs();
                             s.remove(settingsKey(key));
                             applyWidths(table, defaultWidths);
                         }
                     });
}
