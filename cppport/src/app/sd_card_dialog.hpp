#pragma once

// The SD card of a grblHAL board (features/SDCard): whether one is mounted,
// the files on it - run or delete them - and uploads over YMODEM with their
// progress. Tools > SD Card; it stays alive once opened so an upload is
// followed to its end.

#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <vector>

class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace gs::app {

class Machine;
class NotificationCenter;

// validateSDFilename(): the firmware's filename_valid() - the reason a name
// is refused, or nullopt.
std::optional<QString> sdFilenameProblem(const QString& name);
// ACCEPTED_EXTENSIONS: the CNC files the card lists, macros and JSON.
bool isAcceptedSdFile(const QString& name);
// formatFileSize(): B, KB, MB or GB, one decimal above bytes.
QString formatSdFileSize(long long bytes);
// isFileATCIRelated(): the tool changer's own macros (ATC templates are not
// ported, so only its two fixed names).
bool isAtciFile(const QString& name);

// The files an upload takes: the accepted ones, and "name: reason" for the
// others.
struct SdFileCheck {
    QStringList accepted;
    QStringList refused;
};
SdFileCheck checkSdFiles(const QStringList& paths);

// Upload Files (UploadModal): files browsed for or dropped in, each with its
// size and a remove button; Upload sends them all.
class SdUploadDialog final : public QDialog {
    Q_OBJECT
public:
    // `refused` hears "Some files were rejected:..." (a toast upstream).
    explicit SdUploadDialog(std::function<void(const QString&)> refused, QWidget* parent = nullptr);

    void addFiles(const QStringList& paths);
    void removeFile(int index);
    QStringList files() const { return files_; }

protected:
    void dragEnterEvent(class QDragEnterEvent* event) override;
    void dropEvent(class QDropEvent* event) override;

private:
    void browse();
    void refresh();

    std::function<void(const QString&)> refused_;
    QStringList files_;
    QLabel* selected_;
    QListWidget* list_;
    QPushButton* upload_;
};

class SdCardDialog final : public QDialog {
    Q_OBJECT
public:
    SdCardDialog(Machine& machine, NotificationCenter& notifications, QWidget* parent = nullptr);

    // Delete's confirmation, and the files the Upload modal would give
    // (tests answer instead of the user).
    using Confirmer = std::function<bool(const QString& title, const QString& text)>;
    void setConfirmer(Confirmer confirmer) { confirmer_ = std::move(confirmer); }
    using FilePicker = std::function<QStringList()>;
    void setFilePicker(FilePicker picker) { picker_ = std::move(picker); }

    // Refresh Files: the list emptied and the card listed afresh.
    void refreshFiles();
    // Upload: the modal's files go up.
    void openUpload();
    // A drop on the list: checked as the modal checks them, the refused
    // reported and the rest sent. The names sent.
    QStringList upload(const QStringList& paths);
    void runFile(const QString& name);
    // Asks first; the file leaves the list at once.
    void deleteFile(const QString& name);

    // ---- inspection (tests) ----
    QString status() const;   // Mounted, Unmounted or Disconnected
    QString message() const;  // why there is no list, or empty while one shows
    QStringList fileNames() const;
    bool canRun(const QString& name) const;
    bool canDelete(const QString& name) const;
    QPushButton* refreshButton() const { return refresh_; }
    QPushButton* uploadButton() const { return upload_; }
    // idle, uploading or complete; the file's progress while uploading.
    QString uploadState() const { return uploadState_; }
    int uploadProgress() const;

protected:
    void showEvent(QShowEvent* event) override;
    void dragEnterEvent(class QDragEnterEvent* event) override;
    void dropEvent(class QDropEvent* event) override;

private:
    struct Row {
        std::string name;
        long long size = 0;
        bool unusable = false;
        bool runnable = false;
        bool deletable = false;
        bool operator==(const Row&) const = default;
    };

    void refresh();
    void setUploadState(const QString& state);
    bool confirm(const QString& title, const QString& text);
    bool toolsAvailable(QString* reason) const;
    std::vector<Row> rows() const;

    Machine& machine_;
    NotificationCenter& notifications_;
    Confirmer confirmer_;
    FilePicker picker_;
    QLabel* status_;
    QPushButton* refresh_;
    QPushButton* upload_;
    QWidget* actions_;
    QProgressBar* progress_;
    QLabel* complete_;
    QStackedWidget* pages_;
    QLabel* message_;
    QLabel* title_;
    QTableWidget* table_;
    QString uploadState_ = QStringLiteral("idle");
    QSet<QString> deleted_;   // gone from the list until the card is listed again
    std::vector<Row> shown_;  // the rows the table has
};

}  // namespace gs::app
