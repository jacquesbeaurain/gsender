#include "main_window.hpp"

#include "controls.hpp"
#include "machine.hpp"
#include "panels.hpp"
#include "toolpath_view.hpp"

#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

namespace gs::app {

MainWindow::MainWindow(Machine& machine, QWidget* parent) : QMainWindow(parent), machine_(machine) {
    setWindowTitle(tr("gSender (C++)"));

    auto* central = new QWidget;
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new ConnectionBar(machine_));

    auto* splitter = new QSplitter(Qt::Horizontal);
    auto* left = new QWidget;
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(6, 0, 0, 6);
    visualizer_ = new ToolpathView(machine_);
    leftLayout->addWidget(visualizer_, 1);
    leftLayout->addWidget(new JobPanel(machine_));

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 6, 6);
    rightLayout->addWidget(new PositionPanel(machine_));
    auto* tabs = new QTabWidget;
    tabs->addTab(new JogPanel(machine_), tr("Jog"));
    tabs->addTab(new SpindlePanel(machine_), tr("Spindle && Coolant"));
    tabs->addTab(new MacrosPanel(machine_), tr("Macros"));
    rightLayout->addWidget(tabs);
    console_ = new ConsolePanel(machine_);
    rightLayout->addWidget(console_, 1);

    splitter->addWidget(left);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Ready"));
    connect(&machine_, &Machine::notice, this, [this](const QString& text) {
        statusBar()->showMessage(text, 15000);
        console_->append(text, false);
    });
    connect(&machine_, &Machine::connectionFailed, this,
            [this](const QString& reason) { showError(tr("Connection"), reason); });
    connect(&machine_, &Machine::errorReported, this, &MainWindow::showError);
}

void MainWindow::showError(const QString& title, const QString& detail) {
    statusBar()->showMessage(title + ": " + detail.section('\n', 0, 0), 15000);
    console_->append(title + ": " + detail, false);
    if (!dialogsEnabled_) {
        return;
    }
    // Non-modal: the machine keeps reporting while the message is open.
    auto* box = new QMessageBox(QMessageBox::Warning, title, detail, QMessageBox::Ok, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

}  // namespace gs::app
