#pragma once

// G-code Step Through (features/GcodeStepper) for QML: the loaded file line
// by line - the source with the current line and the search's matches, the
// toolpath up to the line with the cutter where the line leaves it (Step
// ToolpathItem), the tools with their eye buttons, the position and the
// modal state - stepped, scrubbed or played back. The line index is built in
// the background.

#include "toolpath_item.hpp"

#include "gs/job/step_through.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

class QTimer;

namespace gs::app {
class Machine;
}

namespace gs::ui {

// The source panel's rows: number, the code (syntax-coloured styled text),
// current, matched.
class StepSourceModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS

public:
    enum Role { NumberRole = Qt::UserRole + 1, CodeRole, CurrentRole, MatchRole };

    explicit StepSourceModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

    void setLines(const std::vector<std::string>* lines, bool dark);
    void setCurrent(std::size_t line);
    void setMatches(const std::vector<std::size_t>& matches);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void changed(std::size_t line);

    const std::vector<std::string>* lines_ = nullptr;
    std::vector<bool> matched_;
    std::size_t current_ = 0;
    bool dark_ = false;
};

class StepThroughModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QObject* source READ source CONSTANT)
    Q_PROPERTY(int total READ total NOTIFY loaded)
    Q_PROPERTY(int line READ line NOTIFY lineChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY lineChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY lineChanged)
    Q_PROPERTY(bool hideProcessed READ hideProcessed WRITE setHideProcessed NOTIFY lineChanged)
    Q_PROPERTY(bool indexReady READ indexReady NOTIFY indexChanged)
    Q_PROPERTY(int indexProgress READ indexProgress NOTIFY indexChanged)  // percent
    // The status: the position in the workspace units; the modals
    // [{label, value, changed}].
    Q_PROPERTY(QString positionTitle READ positionTitle NOTIFY lineChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY lineChanged)
    Q_PROPERTY(QVariantList modals READ modals NOTIFY lineChanged)
    // The tools [{index, number, comment, color, textColor, details, range,
    // startLine, hidden}] and the one the line is in.
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(int activeTool READ activeTool NOTIFY lineChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY matchesChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY matchesChanged)
    Q_PROPERTY(QString lineText READ lineText NOTIFY lineChanged)

public:
    explicit StepThroughModel(QObject* parent = nullptr);
    ~StepThroughModel() override;

    QObject* source() { return &source_; }
    int total() const { return static_cast<int>(lines_.size()); }
    int line() const { return static_cast<int>(line_); }
    bool playing() const { return playing_; }
    double speed() const { return speed_; }
    void setSpeed(double speed);
    bool hideProcessed() const { return hideProcessed_; }
    void setHideProcessed(bool hide);
    bool indexReady() const { return indexReady_; }
    int indexProgress() const { return indexProgress_; }
    QString positionTitle() const;
    QString positionText() const;
    QVariantList modals() const;
    QVariantList tools() const;
    int activeTool() const;
    int matchCount() const { return static_cast<int>(matches_.size()); }
    bool searching() const { return searching_; }
    QString lineText() const;

    // The loaded file, again (opening the screen).
    Q_INVOKABLE void load();
    // goToLine(): clamped to the file; a manual move stops playback.
    Q_INVOKABLE void goToLine(int line);
    Q_INVOKABLE void step(int delta);
    Q_INVOKABLE void reset();
    // From the start again at the end; at the file's estimated pace times
    // the speed.
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stopPlaying();
    Q_INVOKABLE void toggleTool(int index);
    Q_INVOKABLE void setSearch(const QString& query);
    Q_INVOKABLE void goToNextMatch();
    Q_INVOKABLE QStringList quarterLabels() const;
    // Waits for the index (tests).
    Q_INVOKABLE bool waitForIndex(int timeoutMs);

    // For the toolpath item.
    app::Machine& machine() const { return machine_; }
    gcode::Vec4 position() const;
    std::size_t done() const;
    struct Span {
        std::size_t first = 0;
        std::size_t end = 0;
        QColor color;
        bool hidden = false;
    };
    const std::vector<Span>& spans() const { return spans_; }

Q_SIGNALS:
    void loaded();
    void lineChanged();
    void indexChanged();
    void toolsChanged();
    void matchesChanged();

private:
    void setLine(double line);  // playback and scrubbing: keeps playing
    void buildIndex();
    void updateSpans();
    void tick();

    app::Machine& machine_;
    StepSourceModel source_;
    std::string text_;
    std::vector<std::string> lines_;
    job::StepIndex index_;
    bool indexReady_ = false;
    int indexProgress_ = 0;
    std::uint64_t generation_ = 0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::vector<job::StepperTool> tools_;
    std::vector<bool> hiddenTools_;
    std::vector<Span> spans_;
    std::vector<std::size_t> matches_;
    bool searching_ = false;
    std::size_t line_ = 1;
    bool hideProcessed_ = false;
    bool playing_ = false;
    double speed_ = 10;
    double playStartLine_ = 1;
    QElapsedTimer playClock_;
    QTimer* playTimer_;
};

// The step-through's toolpath: the lines before the current one drawn as
// done (grey) or left out, each tool's cuts in its colour (hidden tools
// left out), the cutter where the line leaves it.
class StepToolpathItem : public ToolpathItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(gs::ui::StepThroughModel* model READ model WRITE setModel NOTIFY modelChanged)

public:
    explicit StepToolpathItem(QQuickItem* parent = nullptr) : ToolpathItem(parent) {}

    StepThroughModel* model() const { return model_; }
    void setModel(StepThroughModel* model);

Q_SIGNALS:
    void modelChanged();

protected:
    void paintContent(QPainter& painter, app::ToolpathCamera& camera) override;
    std::optional<gcode::BoundingBox> contentBounds() const override;

private:
    bool rotaryJob() const;

    StepThroughModel* model_ = nullptr;
};

}  // namespace gs::ui
