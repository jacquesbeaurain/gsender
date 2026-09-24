#include "sd_card_dialog.hpp"

#include "machine.hpp"
#include "notifications.hpp"

#include "gs/controller/controller.hpp"
#include "gs/util/jsnumber.hpp"

#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace gs::app {

namespace {

const QStringList kAcceptedExtensions{".gcode", ".nc",   ".ncc", ".ngc",   ".cnc",
                                      ".txt",   ".text", ".tap", ".macro", ".json"};

// The file dialog's filter, as the upload's accept attribute.
QString fileFilter() {
    QStringList patterns;
    for (const QString& extension : kAcceptedExtensions) {
        patterns << "*" + extension;
    }
    return QObject::tr("SD card files (%1)").arg(patterns.join(' '));
}

QStringList droppedFiles(const QMimeData* data) {
    QStringList paths;
    for (const QUrl& url : data->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    return paths;
}

QString refusedText(const QStringList& refused) {
    return QObject::tr("Some files were rejected:\n%1").arg(refused.join('\n'));
}

}  // namespace

std::optional<QString> sdFilenameProblem(const QString& name) {
    if (name.size() > 40) {
        return QObject::tr("Filename too long (max 40 characters)");
    }
    for (const QChar c : {QChar('?'), QChar('~'), QChar('!')}) {
        if (name.contains(c)) {
            return QObject::tr("Filename contains invalid character: %1").arg(c);
        }
    }
    return std::nullopt;
}

bool isAcceptedSdFile(const QString& name) {
    // '.' + the text after the last dot, lower case.
    return kAcceptedExtensions.contains("." + name.section('.', -1).toLower());
}

QString formatSdFileSize(long long bytes) {
    static const char* kUnits[] = {"B", "KB", "MB", "GB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    while (size >= 1024 && unit < 3) {
        size /= 1024;
        ++unit;
    }
    return QString::fromStdString(js::toFixed(size, unit == 0 ? 0 : 1)) + " " + kUnits[unit];
}

bool isAtciFile(const QString& name) {
    return name == "ATCI.macro" || name == "P100.macro";
}

SdFileCheck checkSdFiles(const QStringList& paths) {
    SdFileCheck check;
    for (const QString& path : paths) {
        const QString name = QFileInfo(path).fileName();
        if (!isAcceptedSdFile(name)) {
            check.refused << QObject::tr("%1: Invalid file type").arg(name);
        } else if (const std::optional<QString> problem = sdFilenameProblem(name)) {
            check.refused << name + ": " + *problem;
        } else {
            check.accepted << path;
        }
    }
    return check;
}

// ---- the upload modal ----------------------------------------------------------------------

SdUploadDialog::SdUploadDialog(std::function<void(const QString&)> refused, QWidget* parent)
    : QDialog(parent), refused_(std::move(refused)) {
    setWindowTitle(tr("Upload Files"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Upload one or more valid gcode files to your SD card")));

    auto* drop = new QFrame;
    drop->setObjectName("dropArea");
    drop->setStyleSheet("#dropArea { border: 2px dashed palette(mid); border-radius: 8px; }");
    auto* dropLayout = new QVBoxLayout(drop);
    dropLayout->setContentsMargins(24, 24, 24, 24);
    auto* prompt = new QHBoxLayout;
    prompt->addStretch(1);
    prompt->addWidget(new QLabel(tr("Drag & drop files here, or")));
    auto* browse = new QPushButton(tr("browse"));
    browse->setObjectName("browse");
    browse->setFlat(true);
    browse->setCursor(Qt::PointingHandCursor);
    browse->setStyleSheet("QPushButton { color: #3b82f6; font-weight: 600; }");
    connect(browse, &QPushButton::clicked, this, &SdUploadDialog::browse);
    prompt->addWidget(browse);
    prompt->addStretch(1);
    dropLayout->addLayout(prompt);
    auto* accepts = new QLabel(tr("Accepts: .gcode, .nc, .macro and other supported files"));
    accepts->setAlignment(Qt::AlignCenter);
    accepts->setStyleSheet("color: palette(mid);");
    dropLayout->addWidget(accepts);
    layout->addWidget(drop);

    selected_ = new QLabel;
    layout->addWidget(selected_);
    list_ = new QListWidget;
    list_->setMaximumHeight(240);
    layout->addWidget(list_);

    auto* buttons = new QDialogButtonBox;
    buttons->addButton(QDialogButtonBox::Cancel);
    upload_ = buttons->addButton(tr("Upload"), QDialogButtonBox::AcceptRole);
    upload_->setObjectName("upload");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    refresh();
    resize(460, 380);
}

void SdUploadDialog::browse() {
    addFiles(QFileDialog::getOpenFileNames(this, tr("Upload Files"), QString(), fileFilter()));
}

void SdUploadDialog::addFiles(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }
    const SdFileCheck check = checkSdFiles(paths);
    if (!check.refused.isEmpty() && refused_) {
        refused_(refusedText(check.refused));
    }
    files_ << check.accepted;
    refresh();
}

void SdUploadDialog::removeFile(int index) {
    if (index >= 0 && index < files_.size()) {
        files_.removeAt(index);
        refresh();
    }
}

void SdUploadDialog::refresh() {
    list_->clear();
    for (int index = 0; index < files_.size(); ++index) {
        const QFileInfo info(files_[index]);
        auto* item = new QListWidgetItem(list_);
        auto* row = new QWidget;
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(6, 2, 2, 2);
        auto* name = new QLabel(info.fileName());
        name->setToolTip(files_[index]);
        rowLayout->addWidget(name, 1);
        auto* size = new QLabel(formatSdFileSize(info.size()));
        size->setStyleSheet("color: palette(mid);");
        rowLayout->addWidget(size);
        auto* remove = new QToolButton;
        remove->setText(QStringLiteral("✕"));
        remove->setToolTip(tr("Remove file"));
        remove->setAutoRaise(true);
        connect(remove, &QToolButton::clicked, this, [this, index] { removeFile(index); });
        rowLayout->addWidget(remove);
        item->setSizeHint(row->sizeHint());
        list_->setItemWidget(item, row);
    }
    selected_->setText(tr("Selected Files (%1):").arg(files_.size()));
    selected_->setVisible(!files_.isEmpty());
    list_->setVisible(!files_.isEmpty());
    upload_->setText(files_.isEmpty() ? tr("Upload") : tr("Upload (%1)").arg(files_.size()));
    upload_->setEnabled(!files_.isEmpty());
}

void SdUploadDialog::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void SdUploadDialog::dropEvent(QDropEvent* event) {
    addFiles(droppedFiles(event->mimeData()));
    event->acceptProposedAction();
}

// ---- the SD card ---------------------------------------------------------------------------

namespace {
enum Column { kName, kSize, kFlags, kActions, kColumns };
}

SdCardDialog::SdCardDialog(Machine& machine, NotificationCenter& notifications, QWidget* parent)
    : QDialog(parent), machine_(machine), notifications_(notifications) {
    setWindowTitle(tr("SD Card"));
    setAcceptDrops(true);
    auto* layout = new QVBoxLayout(this);

    // StatusIndicator: the card's status, and the actions or the upload's
    // progress beside it.
    auto* top = new QHBoxLayout;
    auto* statusBox = new QFrame;
    statusBox->setFrameShape(QFrame::StyledPanel);
    auto* statusLayout = new QHBoxLayout(statusBox);
    statusLayout->addWidget(new QLabel(tr("SD Card Status:")));
    statusLayout->addStretch(1);
    status_ = new QLabel;
    status_->setObjectName("sdStatus");
    statusLayout->addWidget(status_);
    top->addWidget(statusBox, 1);

    auto* actionBox = new QFrame;
    actionBox->setFrameShape(QFrame::StyledPanel);
    auto* actionLayout = new QHBoxLayout(actionBox);
    actions_ = new QWidget;
    auto* buttons = new QHBoxLayout(actions_);
    buttons->setContentsMargins(0, 0, 0, 0);
    refresh_ = new QPushButton(tr("Refresh Files"));
    refresh_->setObjectName("refreshFiles");
    connect(refresh_, &QPushButton::clicked, this, &SdCardDialog::refreshFiles);
    buttons->addWidget(refresh_);
    upload_ = new QPushButton(tr("Upload"));
    upload_->setObjectName("uploadFiles");
    connect(upload_, &QPushButton::clicked, this, &SdCardDialog::openUpload);
    buttons->addWidget(upload_);
    actionLayout->addWidget(actions_);
    progress_ = new QProgressBar;
    progress_->setRange(0, 100);
    progress_->setFormat(tr("Uploading... %p%"));
    actionLayout->addWidget(progress_, 1);
    complete_ = new QLabel(tr("✔ Upload complete!"));
    complete_->setStyleSheet("color: #16a34a; font-weight: 600;");
    actionLayout->addWidget(complete_);
    top->addWidget(actionBox, 1);
    layout->addLayout(top);

    // FileList: a message, or the files.
    pages_ = new QStackedWidget;
    message_ = new QLabel;
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setFrameShape(QFrame::StyledPanel);
    message_->setMinimumHeight(160);
    pages_->addWidget(message_);
    auto* listPage = new QWidget;
    auto* listLayout = new QVBoxLayout(listPage);
    listLayout->setContentsMargins(0, 0, 0, 0);
    title_ = new QLabel;
    title_->setStyleSheet("font-weight: 600; font-size: 13pt;");
    listLayout->addWidget(title_);
    table_ = new QTableWidget(0, kColumns);
    table_->setHorizontalHeaderLabels({tr("File Name"), tr("Size"), QString(), tr("Actions")});
    table_->horizontalHeader()->setSectionResizeMode(kName, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(kSize, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kFlags, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(kActions, QHeaderView::ResizeToContents);
    table_->verticalHeader()->hide();
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    listLayout->addWidget(table_, 1);
    pages_->addWidget(listPage);
    layout->addWidget(pages_, 1);

    connect(&machine_, &Machine::connectionChanged, this, [this] {
        if (!machine_.isConnected()) {
            deleted_.clear();
            setUploadState("idle");  // the upload went with the connection
        }
        refresh();
    });
    connect(&machine_, &Machine::stateChanged, this, &SdCardDialog::refresh);
    connect(&machine_, &Machine::settingsChanged, this, &SdCardDialog::refresh);
    connect(&machine_, &Machine::workflowChanged, this, &SdCardDialog::refresh);
    connect(&machine_, &Machine::sdUploadStarted, this, [this] {
        progress_->setValue(0);
        setUploadState("uploading");
    });
    connect(&machine_, &Machine::sdUploadProgress, this, [this](int percent) { progress_->setValue(percent); });
    connect(&machine_, &Machine::sdUploadCompleted, this, [this] {
        setUploadState("complete");
        QTimer::singleShot(1000, this, [this] {
            if (uploadState_ == "complete") {
                setUploadState("idle");
            }
        });
    });
    connect(&machine_, &Machine::sdUploadFailed, this, [this](const QString& error) {
        setUploadState("idle");
        notifications_.add(tr("Error uploading file - %1.").arg(error), NotificationType::Error);
    });
    setUploadState("idle");
    refresh();
    resize(760, 480);
}

bool SdCardDialog::toolsAvailable(QString* reason) const {
    controller::Controller* c = machine_.controller();
    if (!machine_.isConnected() || !c) {
        *reason = tr("Must be connected to use SD card functionality.");
        return false;
    }
    if (!c->isGrblHal()) {
        *reason = tr("SD card tools are only available for grblHAL devices.");
        return false;
    }
    const auto& info = c->runner().settings().info;
    const auto newopt = info.find("NEWOPT");
    if (newopt == info.end() || !(newopt->second.hasOption("FTP") || newopt->second.hasOption("YM"))) {
        *reason = tr("Enable FTP or YMODEM in firmware to use SD card tools.");
        return false;
    }
    return true;
}

std::vector<SdCardDialog::Row> SdCardDialog::rows() const {
    std::vector<Row> rows;
    controller::Controller* c = machine_.controller();
    if (!c) {
        return rows;
    }
    const protocol::RunnerState& state = c->state();
    // A file running from the card reports its name in the status.
    const bool runningSdFile = state.status.sdProgress.name.has_value();
    const bool idle = c->workflow().isIdle();
    for (const protocol::SdFile& file : state.sdcard.files) {
        const QString name = QString::fromStdString(file.name);
        if (deleted_.contains(name)) {
            continue;
        }
        Row row{file.name, file.size, file.unusable};
        row.deletable = !runningSdFile && idle;
        row.runnable = row.deletable && !isAtciFile(name) && !file.unusable;
        rows.push_back(std::move(row));
    }
    return rows;
}

void SdCardDialog::refresh() {
    controller::Controller* c = machine_.controller();
    const bool connected = machine_.isConnected() && c;
    const bool mounted = connected && c->state().status.sdCard;
    status_->setText(!connected ? tr("Disconnected") : mounted ? tr("Mounted") : tr("Unmounted"));
    status_->setStyleSheet(QString("QLabel { border: 2px solid %1; border-radius: 12px; padding: 4px 14px; "
                                   "font-weight: 600; color: %2; }")
                               .arg(!connected ? "#d1d5db" : mounted ? "#bbf7d0" : "#fecaca",
                                    !connected ? "#374151" : mounted ? "#15803d" : "#b91c1c"));

    QString reason;
    const bool available = toolsAvailable(&reason);
    refresh_->setEnabled(available);
    upload_->setEnabled(available && mounted);

    // Names deleted here come back only when the card lists them again.
    if (c) {
        QSet<QString> listed;
        for (const protocol::SdFile& file : c->state().sdcard.files) {
            listed.insert(QString::fromStdString(file.name));
        }
        deleted_.intersect(listed);
    }

    std::vector<Row> rows = available ? this->rows() : std::vector<Row>{};
    if (!available) {
        message_->setText(reason);
        pages_->setCurrentWidget(message_);
    } else if (rows.empty()) {
        message_->setText(tr("<p style='font-size: 13pt;'><b>No files found</b></p>"
                             "<p>Upload files or refresh to see SD card contents</p>"));
        pages_->setCurrentWidget(message_);
    } else {
        pages_->setCurrentIndex(1);
    }
    title_->setText(tr("Files (%1)").arg(rows.size()));
    if (rows == shown_) {
        return;
    }
    shown_ = rows;
    table_->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const Row& row = rows[static_cast<std::size_t>(r)];
        const QString name = QString::fromStdString(row.name);
        const bool atci = isAtciFile(name);
        auto* nameItem = new QTableWidgetItem(name);
        table_->setItem(r, kName, nameItem);
        table_->setItem(r, kSize, new QTableWidgetItem(formatSdFileSize(row.size)));
        // The ATC's macros are marked, as are the files the firmware refuses.
        auto* flags = new QWidget;
        auto* flagLayout = new QHBoxLayout(flags);
        flagLayout->setContentsMargins(4, 0, 4, 0);
        if (atci) {
            auto* label = new QLabel(tr("ATC Macro"));
            QFont italic = label->font();
            italic.setItalic(true);
            label->setFont(italic);
            flagLayout->addWidget(label);
        }
        if (row.unusable) {
            auto* label = new QLabel(tr("Unusable"));
            label->setStyleSheet("QLabel { color: #dc2626; background: #fef2f2; border-radius: 4px; "
                                 "padding: 1px 6px; font-size: 8pt; font-weight: 600; }");
            label->setToolTip(sdFilenameProblem(name).value_or(tr("File flagged as unusable by firmware")));
            flagLayout->addWidget(label);
        }
        flagLayout->addStretch(1);
        table_->setCellWidget(r, kFlags, flags);
        auto* actions = new QWidget;
        auto* actionLayout = new QHBoxLayout(actions);
        actionLayout->setContentsMargins(4, 0, 4, 0);
        auto* run = new QPushButton(tr("▶ Run"));
        run->setObjectName("run");
        run->setEnabled(row.runnable);
        connect(run, &QPushButton::clicked, this, [this, name] { runFile(name); });
        actionLayout->addWidget(run);
        auto* remove = new QPushButton(tr("Delete"));
        remove->setObjectName("delete");
        remove->setEnabled(row.deletable);
        remove->setStyleSheet("QPushButton:enabled { color: #dc2626; }");
        connect(remove, &QPushButton::clicked, this, [this, name] { deleteFile(name); });
        actionLayout->addWidget(remove);
        table_->setCellWidget(r, kActions, actions);
        if (atci) {
            // Upstream stripes them yellow.
            for (int column = kName; column <= kSize; ++column) {
                table_->item(r, column)->setBackground(QColor(254, 252, 232));
            }
        }
    }
}

void SdCardDialog::setUploadState(const QString& state) {
    uploadState_ = state;
    actions_->setVisible(state == "idle");
    progress_->setVisible(state == "uploading");
    complete_->setVisible(state == "complete");
}

int SdCardDialog::uploadProgress() const {
    return progress_->value();
}

void SdCardDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    // Opening lists the card afresh, when the board can take commands.
    controller::Controller* c = machine_.controller();
    QString reason;
    if (c && toolsAvailable(&reason) && uploadState_ == "idle") {
        const std::string& state = c->state().status.activeState;
        if (state == "Idle" || state == "Alarm") {
            refreshFiles();
        }
    }
    refresh();
}

void SdCardDialog::refreshFiles() {
    if (controller::Controller* c = machine_.controller(); c && c->isGrblHal()) {
        deleted_.clear();
        c->sdList();
    }
}

void SdCardDialog::openUpload() {
    QStringList paths;
    if (picker_) {
        paths = picker_();
    } else {
        SdUploadDialog dialog([this](const QString& text) { notifications_.add(text, NotificationType::Error); },
                              this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        paths = dialog.files();
    }
    upload(paths);
}

QStringList SdCardDialog::upload(const QStringList& paths) {
    const SdFileCheck check = checkSdFiles(paths);
    if (!check.refused.isEmpty()) {
        notifications_.add(refusedText(check.refused), NotificationType::Error);
    }
    controller::Controller* c = machine_.controller();
    if (!c || check.accepted.isEmpty()) {
        return {};
    }
    std::vector<protocol::YModemFile> files;
    QStringList names;
    for (const QString& path : check.accepted) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            notifications_.add(tr("Error uploading file - could not read %1.").arg(path), NotificationType::Error);
            continue;
        }
        const QByteArray bytes = file.readAll();
        const QString name = QFileInfo(path).fileName();
        files.push_back({name.toStdString(), std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))});
        names << name;
    }
    if (!files.empty()) {
        c->sdUpload(std::move(files));
    }
    return names;
}

void SdCardDialog::runFile(const QString& name) {
    if (controller::Controller* c = machine_.controller(); c && canRun(name)) {
        c->sdRun(name.toStdString());
    }
}

void SdCardDialog::deleteFile(const QString& name) {
    controller::Controller* c = machine_.controller();
    if (!c || !canDelete(name) || !confirm(tr("Delete File"), tr("Are you sure you want to delete %1?").arg(name))) {
        return;
    }
    c->sdDelete(name.toStdString());
    deleted_.insert(name);
    refresh();
}

bool SdCardDialog::confirm(const QString& title, const QString& text) {
    if (confirmer_) {
        return confirmer_(title, text);
    }
    return QMessageBox::question(this, title, text, QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) ==
           QMessageBox::Yes;
}

QString SdCardDialog::status() const {
    return status_->text();
}

QString SdCardDialog::message() const {
    return pages_->currentWidget() == message_ ? message_->text() : QString();
}

QStringList SdCardDialog::fileNames() const {
    QStringList names;
    for (const Row& row : rows()) {
        names << QString::fromStdString(row.name);
    }
    return names;
}

bool SdCardDialog::canRun(const QString& name) const {
    const std::vector<Row> all = rows();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Row& row) { return row.name == name.toStdString(); });
    return it != all.end() && it->runnable;
}

bool SdCardDialog::canDelete(const QString& name) const {
    const std::vector<Row> all = rows();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Row& row) { return row.name == name.toStdString(); });
    return it != all.end() && it->deletable;
}

void SdCardDialog::dragEnterEvent(QDragEnterEvent* event) {
    QString reason;
    if (event->mimeData()->hasUrls() && toolsAvailable(&reason) && uploadState_ == "idle") {
        event->acceptProposedAction();
    }
}

void SdCardDialog::dropEvent(QDropEvent* event) {
    upload(droppedFiles(event->mimeData()));
    event->acceptProposedAction();
}

}  // namespace gs::app
