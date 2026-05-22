#include "features/pit_optimization/dialogs/PitOptimizationReportDialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cmath>
#include <limits>

namespace
{
QString PitReportUnitLabel(const QString &unit)
{
    if (unit == "Percentage") {
        return "%";
    }
    if (unit == "Concentration (g/t)") {
        return "g/t";
    }
    if (unit == "Mass (g)") {
        return "g/t";
    }
    if (unit == "Mass (oz)") {
        return "oz/t";
    }
    if (unit == "Mass (lb)") {
        return "lb/t";
    }
    return unit;
}

QString PitReportNumber(double value)
{
    if (!std::isfinite(value)) {
        return "-";
    }
    return QString::number(value, 'f', 2);
}

QTableWidgetItem *PitReportItem(const QString &text, Qt::Alignment alignment = Qt::AlignRight | Qt::AlignVCenter)
{
    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(alignment);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}
}

PitOptimizationReportDialog::PitOptimizationReportDialog(const PitOptimizationReport &report, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Open Pit Optimization Report");
    setModal(true);
    resize(1180, 620);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *title = new QLabel(
        QString("Open pit optimization report for %1").arg(report.economicModelName),
        this);
    layout->addWidget(title);

    QLabel *note = new QLabel(
        "The economic model summary is calculated before MineFlow. Each RF pit row is cumulative.",
        this);
    note->setWordWrap(true);
    layout->addWidget(note);

    QStringList headers = {"Scenario", "Ore t"};
    for (const QString &product : report.products) {
        const QString unit = PitReportUnitLabel(report.productUnits.value(product));
        headers << (unit.isEmpty()
            ? QString("Ore %1").arg(product)
            : QString("Ore %1 (%2)").arg(product, unit));
    }
    headers << "Waste t" << "Total t";
    for (const QString &destination : report.destinationNames) {
        headers << QString("%1 t").arg(destination);
        for (const QString &product : report.products) {
            const QString unit = PitReportUnitLabel(report.productUnits.value(product));
            headers << (unit.isEmpty()
                ? QString("%1 %2").arg(destination, product)
                : QString("%1 %2 (%3)").arg(destination, product, unit));
        }
    }

    auto makeTable = [&](const QList<PitOptimizationReportRow> &sourceRows, bool appendFinalRow) {
        QList<PitOptimizationReportRow> rows = sourceRows;
        if (appendFinalRow && !rows.isEmpty()) {
            PitOptimizationReportRow finalRow = rows.last();
            finalRow.label = "Final Pit";
            rows.append(finalRow);
        }

        QTableWidget *table = new QTableWidget(rows.size(), headers.size(), this);
        table->setHorizontalHeaderLabels(headers);
        table->verticalHeader()->setVisible(false);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setMinimumSectionSize(95);
        table->setColumnWidth(0, 120);
        for (int col = 1; col < headers.size(); ++col) {
            table->setColumnWidth(col, 120);
        }

        for (int rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
            const PitOptimizationReportRow &row = rows[rowIndex];
            int col = 0;
            table->setItem(rowIndex, col++, PitReportItem(
                row.label.isEmpty() ? QString::number(row.revenueFactor, 'f', 2) : row.label,
                Qt::AlignLeft | Qt::AlignVCenter));
            table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(row.oreTonnes)));
            for (const QString &product : report.products) {
                const double average = row.oreTonnes > 0.0
                    ? row.oreProductWeightedSums.value(product) / row.oreTonnes
                    : std::numeric_limits<double>::quiet_NaN();
                table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(average)));
            }
            table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(row.wasteTonnes)));
            table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(row.totalTonnes)));

            for (const QString &destination : report.destinationNames) {
                const PitOptimizationDestinationReport destinationReport = row.destinations.value(destination);
                table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(destinationReport.tonnes)));
                for (const QString &product : report.products) {
                    const double average = destinationReport.tonnes > 0.0
                        ? destinationReport.productWeightedSums.value(product) / destinationReport.tonnes
                        : std::numeric_limits<double>::quiet_NaN();
                    table->setItem(rowIndex, col++, PitReportItem(PitReportNumber(average)));
                }
            }
        }
        return table;
    };

    QLabel *economicTitle = new QLabel("Economic Model Summary", this);
    layout->addWidget(economicTitle);
    layout->addWidget(makeTable(report.economicModelRows, false), 0);

    QLabel *pitTitle = new QLabel("Cumulative MineFlow Pit Report", this);
    layout->addWidget(pitTitle);
    layout->addWidget(makeTable(report.rows, true), 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}
