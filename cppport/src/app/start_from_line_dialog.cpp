#include "start_from_line_dialog.hpp"

#include "machine.hpp"

#include "gs/util/units.hpp"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace gs::app {

StartFromLineDialog::StartFromLineDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Start From Line"));
    auto* layout = new QVBoxLayout(this);
    const auto total = static_cast<int>(machine_.analysis().totalLines);
    const auto lastLine = static_cast<int>(machine_.lastLine());

    QString text = tr("Recover a job after power loss, mechanical malfunction, disconnection, or other failure.") +
                   "<p>" +
                   tr("Your job of <b>%1</b> lines was last stopped around line: <b>%2</b>.").arg(total).arg(lastLine);
    if (lastLine > 0) {
        text += "<p>" + tr("For best success, we usually recommend resuming about <b>10 lines</b> earlier: "
                           "<b>line %1</b>")
                            .arg(std::max(lastLine - 10, 0));
    }
    auto* intro = new QLabel(text);
    intro->setWordWrap(true);
    intro->setMinimumWidth(380);
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    line_ = new QSpinBox;
    line_->setRange(1, std::max(total, 1));
    line_->setValue(std::max(lastLine - 10, 1));
    safeHeight_ = new QDoubleSpinBox;
    safeHeight_->setRange(0, 500);
    // The safe retract height when set, else 10 mm (0.4 in).
    const bool metric = machine_.settings().metric;
    const double retract = machine_.settings().safeRetractHeight;
    safeHeight_->setDecimals(metric ? 2 : 3);
    safeHeight_->setSuffix(metric ? " mm" : " in");
    safeHeight_->setValue(retract == 0 ? (metric ? 10 : 0.4) : (metric ? retract : units::convertToImperial(retract)));
    safeHeight_->setToolTip(tr("Default value: %1. The bit rises this far above the file's highest Z first.")
                                .arg(metric ? "10 mm" : "0.4 in"));
    form->addRow(tr("Resume job at line:"), line_);
    form->addRow(tr("With safe height:"), safeHeight_);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* startButton = buttons->addButton(tr("Start from Line"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    connect(startButton, &QPushButton::clicked, this, [this] { start(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

int StartFromLineDialog::line() const {
    return line_->value();
}

void StartFromLineDialog::setLine(int line) {
    line_->setValue(line);
}

double StartFromLineDialog::safeHeight() const {
    return machine_.settings().metric ? safeHeight_->value() : safeHeight_->value() * 25.4;
}

bool StartFromLineDialog::start() {
    if (!machine_.startFromLine(static_cast<std::size_t>(line()), safeHeight())) {
        return false;
    }
    Q_EMIT machine_.notice(tr("Running Start From Specific Line Command"));
    accept();
    return true;
}

}  // namespace gs::app
