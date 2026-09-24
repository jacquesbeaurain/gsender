#include "stats_dialog.hpp"

#include "machine.hpp"

#include "gs/config/history.hpp"
#include "gs/util/jsnumber.hpp"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace gs::app {
namespace {

QString when(std::int64_t ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm");
}

QString duration(double ms) {
    const auto total = static_cast<long long>(std::llround(std::max(0.0, ms) / 1000));
    return QString("%1:%2:%3")
        .arg(total / 3600)
        .arg((total % 3600) / 60, 2, 10, QChar('0'))
        .arg(total % 60, 2, 10, QChar('0'));
}

QTableWidget* table(const QStringList& headers) {
    auto* t = new QTableWidget(0, static_cast<int>(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setAlternatingRowColors(true);
    t->horizontalHeader()->setStretchLastSection(true);
    return t;
}

void setRow(QTableWidget* t, int row, const QStringList& cells) {
    for (int column = 0; column < cells.size(); ++column) {
        t->setItem(row, column, new QTableWidgetItem(cells[column]));
    }
}

bool confirm(QWidget* parent, const QString& title, const QString& text) {
    return QMessageBox::question(parent, title, text, QMessageBox::Yes | QMessageBox::Cancel) == QMessageBox::Yes;
}

}  // namespace

StatsDialog::StatsDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Statistics"));
    resize(820, 520);
    auto* layout = new QVBoxLayout(this);
    auto* tabs = new QTabWidget;
    layout->addWidget(tabs, 1);

    // Jobs
    auto* jobsPage = new QWidget;
    auto* jobsLayout = new QVBoxLayout(jobsPage);
    totals_ = new QLabel;
    jobsLayout->addWidget(totals_);
    jobs_ = table({tr("File"), tr("Started"), tr("Duration"), tr("Lines"), tr("Port"), tr("Status")});
    jobsLayout->addWidget(jobs_, 1);
    auto* clearJobs = new QPushButton(tr("Clear Job History"));
    connect(clearJobs, &QPushButton::clicked, this, [this] {
        if (confirm(this, tr("Clear Job History"), tr("Delete the record of every job run?"))) {
            config::JobStatsStore(machine_.config()).clear();
            reload();
        }
    });
    jobsLayout->addWidget(clearJobs, 0, Qt::AlignLeft);
    tabs->addTab(jobsPage, tr("Jobs"));

    // Maintenance
    auto* tasksPage = new QWidget;
    auto* tasksLayout = new QVBoxLayout(tasksPage);
    tasksLayout->addWidget(new QLabel(tr("Hours the machine has run jobs since each task was last done.")));
    tasks_ = table({tr("Task"), tr("Due at (h)"), tr("Run (h)"), tr("Status")});
    tasksLayout->addWidget(tasks_, 1);
    auto* taskButtons = new QHBoxLayout;
    auto* add = new QPushButton(tr("Add..."));
    auto* edit = new QPushButton(tr("Edit..."));
    auto* done = new QPushButton(tr("Mark Done"));
    auto* remove = new QPushButton(tr("Delete"));
    for (QPushButton* b : {add, edit, done, remove}) {
        taskButtons->addWidget(b);
    }
    taskButtons->addStretch(1);
    tasksLayout->addLayout(taskButtons);
    connect(add, &QPushButton::clicked, this, [this] { editTask(-1); });
    connect(edit, &QPushButton::clicked, this, [this] {
        if (const int id = selectedTaskId(); id >= 0) {
            editTask(id);
        }
    });
    connect(tasks_, &QTableWidget::cellDoubleClicked, this, [this] {
        if (const int id = selectedTaskId(); id >= 0) {
            editTask(id);
        }
    });
    connect(done, &QPushButton::clicked, this, [this] {
        if (const int id = selectedTaskId(); id >= 0) {
            config::MaintenanceStore(machine_.config()).markDone(id);
            reload();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int id = selectedTaskId();
        if (id >= 0 && confirm(this, tr("Delete Task"), tr("Delete this maintenance task?"))) {
            config::MaintenanceStore(machine_.config()).remove(id);
            reload();
        }
    });
    tabs->addTab(tasksPage, tr("Maintenance"));

    // Alarms and errors
    auto* alarmsPage = new QWidget;
    auto* alarmsLayout = new QVBoxLayout(alarmsPage);
    alarms_ = table({tr("Time"), tr("Type"), tr("Code"), tr("Message"), tr("Line"), tr("Source")});
    alarmsLayout->addWidget(alarms_, 1);
    auto* clearAlarms = new QPushButton(tr("Clear Alarms && Errors"));
    connect(clearAlarms, &QPushButton::clicked, this, [this] {
        if (confirm(this, tr("Clear Alarms & Errors"), tr("Delete the alarm and error log?"))) {
            config::AlarmHistory(machine_.config()).clear();
            reload();
        }
    });
    alarmsLayout->addWidget(clearAlarms, 0, Qt::AlignLeft);
    tabs->addTab(alarmsPage, tr("Alarms && Errors"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    QPushButton* diagnostics = buttons->addButton(tr("Download Diagnostic File"), QDialogButtonBox::ActionRole);
    diagnostics->setObjectName("downloadDiagnostics");
    diagnostics->setToolTip(tr("Share this file with our customer support or community so others can help you "
                               "better. It contains your machine errors, profile, settings, and more."));
    connect(diagnostics, &QPushButton::clicked, this, &StatsDialog::diagnosticsRequested);
    layout->addWidget(buttons);
    connect(&machine_, &Machine::historyChanged, this, &StatsDialog::reload);
    reload();
}

QString StatsDialog::totalsText() const {
    return totals_->text();
}

void StatsDialog::reload() {
    config::ConfigStore& store = machine_.config();

    const config::JobStats stats = config::JobStatsStore(store).load();
    totals_->setText(tr("Jobs: %1 - completed %2, stopped %3 - run time %4")
                         .arg(stats.totalJobs)
                         .arg(stats.jobsCompleted)
                         .arg(stats.jobsCancelled)
                         .arg(duration(stats.totalRuntime)));
    jobs_->setRowCount(static_cast<int>(stats.jobs.size()));
    int row = 0;
    for (auto it = stats.jobs.rbegin(); it != stats.jobs.rend(); ++it, ++row) {  // newest first
        setRow(jobs_, row,
               {QString::fromStdString(it->file), when(it->startTime), duration(static_cast<double>(it->duration)),
                QString::number(it->totalLines), QString::fromStdString(it->port),
                it->completed ? tr("Complete") : tr("Stopped")});
        jobs_->item(row, 0)->setToolTip(QString::fromStdString(it->path));
    }
    jobs_->resizeColumnsToContents();

    const std::vector<config::MaintenanceTask> tasks = config::MaintenanceStore(store).list();
    tasks_->setRowCount(static_cast<int>(tasks.size()));
    row = 0;
    for (const config::MaintenanceTask& task : tasks) {
        const config::MaintenanceDue due = config::maintenanceDue(task);
        QString status;
        QString color;
        switch (due) {
            case config::MaintenanceDue::Low:
            case config::MaintenanceDue::Soon:
                status = tr("%1 h left").arg(config::hoursUntilDue(task));
                color = due == config::MaintenanceDue::Soon ? "#b45309" : QString();
                break;
            case config::MaintenanceDue::Due:
                status = tr("Due");
                color = "#c2410c";
                break;
            case config::MaintenanceDue::Urgent:
                status = tr("Urgent!");
                color = "#dc2626";
                break;
        }
        setRow(tasks_, row,
               {QString::fromStdString(task.name),
                QString("%1 - %2").arg(task.rangeStart).arg(task.rangeEnd),
                QString::number(task.currentTime, 'f', 1), status});
        tasks_->item(row, 0)->setData(Qt::UserRole, task.id);
        tasks_->item(row, 0)->setToolTip(QString::fromStdString(task.description));
        if (!color.isEmpty()) {
            tasks_->item(row, 3)->setForeground(QColor(color));
        }
        ++row;
    }
    tasks_->resizeColumnsToContents();

    const std::vector<config::AlarmRecord> alarms = config::AlarmHistory(store).list();
    alarms_->setRowCount(static_cast<int>(alarms.size()));
    row = 0;
    for (const config::AlarmRecord& alarm : alarms) {
        const QString line = alarm.lineNumber ? QString("%1: %2")
                                                    .arg(*alarm.lineNumber)
                                                    .arg(QString::fromStdString(alarm.line))
                                              : QString::fromStdString(alarm.line);
        setRow(alarms_, row,
               {when(alarm.time), alarm.alarm ? tr("Alarm") : tr("Error"), QString::fromStdString(alarm.code),
                QString::fromStdString(alarm.message), line, QString::fromStdString(alarm.source)});
        ++row;
    }
    alarms_->resizeColumnsToContents();
}

int StatsDialog::selectedTaskId() const {
    const QList<QTableWidgetItem*> selected = tasks_->selectedItems();
    if (selected.isEmpty()) {
        return -1;
    }
    const QTableWidgetItem* first = tasks_->item(selected.first()->row(), 0);
    return first ? first->data(Qt::UserRole).toInt() : -1;
}

void StatsDialog::editTask(int id) {
    config::MaintenanceStore store(machine_.config());
    config::MaintenanceTask task;
    if (id >= 0) {
        for (const config::MaintenanceTask& existing : store.list()) {
            if (existing.id == id) {
                task = existing;
            }
        }
    } else {
        task.rangeStart = 10;
        task.rangeEnd = 20;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(id >= 0 ? tr("Edit Maintenance Task") : tr("Add Maintenance Task"));
    auto* form = new QFormLayout(&dialog);
    auto* name = new QLineEdit(QString::fromStdString(task.name));
    auto* description = new QPlainTextEdit(QString::fromStdString(task.description));
    auto* start = new QDoubleSpinBox;
    auto* end = new QDoubleSpinBox;
    for (QDoubleSpinBox* box : {start, end}) {
        box->setRange(0, 100000);
        box->setDecimals(0);
        box->setSuffix(tr(" h"));
    }
    start->setValue(task.rangeStart);
    end->setValue(task.rangeEnd);
    form->addRow(tr("Name"), name);
    form->addRow(tr("Description"), description);
    form->addRow(tr("Due from"), start);
    form->addRow(tr("Due until"), end);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted || name->text().trimmed().isEmpty()) {
        return;
    }
    task.name = name->text().trimmed().toStdString();
    task.description = description->toPlainText().toStdString();
    task.rangeStart = start->value();
    task.rangeEnd = std::max(end->value(), start->value());
    if (id >= 0) {
        store.update(task);
    } else {
        store.add(task);
    }
    reload();
}

}  // namespace gs::app
