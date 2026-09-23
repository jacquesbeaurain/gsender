#include "main_window.hpp"

#include "controls.hpp"
#include "machine.hpp"
#include "panels.hpp"
#include "settings_dialog.hpp"
#include "toolpath_view.hpp"

#include <QApplication>
#include <QFileDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
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
    // A "Code" tool change: the pre-hook ran, now the operator changes the
    // tool and continues (gSender's tool change dialog).
    connect(&machine_, &Machine::toolChangeWaiting, this, [this](const QString& comment) {
        statusBar()->showMessage(tr("Change the tool, then continue."));
        if (!dialogsEnabled_) {
            return;
        }
        auto* box = new QMessageBox(QMessageBox::Information, tr("Tool change"),
                                    tr("Change the tool, then continue the job.") +
                                        (comment.isEmpty() ? QString() : "\n\n" + comment),
                                    QMessageBox::NoButton, this);
        QPushButton* next = box->addButton(tr("Continue"), QMessageBox::AcceptRole);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->setModal(false);
        connect(next, &QPushButton::clicked, this, [this] {
            if (auto* c = machine_.controller()) {
                c->toolChangePost();
            }
        });
        box->show();
    });
    createMenus();
}

void MainWindow::createMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    QAction* open = file->addAction(tr("&Load File..."), this, &MainWindow::openFile);
    open->setShortcut(QKeySequence::Open);
    file->addAction(tr("&Close File"), &machine_, &Machine::unloadProgram);
    file->addSeparator();
    QAction* quit = file->addAction(tr("&Quit"), qApp, &QApplication::quit);
    quit->setShortcut(QKeySequence::Quit);

    QMenu* machine = menuBar()->addMenu(tr("&Machine"));
    QAction* settings = machine->addAction(tr("&Settings..."), this, [this] { openSettings(0); });
    settings->setShortcut(QKeySequence::Preferences);
    machine->addAction(tr("&Firmware Settings..."), this, [this] { openSettings(2); });

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About gSender (C++)"),
                           tr("<b>gSender (C++)</b> %1<p>A native C++/Qt port of gSender, the CNC control "
                              "software for Grbl and grblHAL by Sienci Labs.</p>")
                               .arg(QApplication::applicationVersion()));
    });
}

void MainWindow::openFile() {
    controller::Controller* c = machine_.controller();
    if (c && !c->workflow().isIdle()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load G-code"), QString(), tr("G-code (*.nc *.gcode *.gc *.ngc *.tap *.cnc *.txt);;All files (*)"));
    if (path.isEmpty()) {
        return;
    }
    QString error;
    if (!machine_.loadFile(path, &error)) {
        showError(tr("Load file"), tr("Cannot open %1: %2").arg(path, error));
    }
}

void MainWindow::openSettings(int page) {
    SettingsDialog dialog(machine_, this);
    dialog.showPage(static_cast<SettingsDialog::Page>(page));
    dialog.exec();
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
