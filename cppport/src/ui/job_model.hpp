#pragma once

// Job control (features/JobControl) for QML: Start / Pause / Stop with
// ControlButton's rules, Outline and Start From Line, the progress while a
// job runs (ProgressArea, SDCardProgress), and the feed and spindle
// overrides (FeedOverride).

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class JobModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(bool canPause READ canPause NOTIFY changed)
    Q_PROPERTY(bool canStop READ canStop NOTIFY changed)
    // Outline and Start From Line: a file ready and the machine idle.
    Q_PROPERTY(bool canPrepare READ canPrepare NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    // Progress: shown once lines have gone out; the line running of the total,
    // the elapsed and remaining time; or the SD card file the board runs.
    Q_PROPERTY(bool showProgress READ showProgress NOTIFY progressChanged)
    Q_PROPERTY(int received READ received NOTIFY progressChanged)
    Q_PROPERTY(int total READ total NOTIFY progressChanged)
    Q_PROPERTY(double percent READ percent NOTIFY progressChanged)
    Q_PROPERTY(QString elapsed READ elapsed NOTIFY progressChanged)    // "0:01:05"
    Q_PROPERTY(QString remaining READ remaining NOTIFY progressChanged)
    Q_PROPERTY(QString sdFile READ sdFile NOTIFY progressChanged)      // "" unless the card runs one
    // Start From Line: the job's lines, where it last stopped, the safe
    // height it starts with (workspace units) and its unit.
    Q_PROPERTY(int totalLines READ totalLines NOTIFY changed)
    Q_PROPERTY(int lastLine READ lastLine NOTIFY changed)
    Q_PROPERTY(double defaultSafeHeight READ defaultSafeHeight NOTIFY changed)
    Q_PROPERTY(QString units READ units NOTIFY changed)
    // Overrides (%) and what they act on: the feed in the workspace units a
    // minute, the spindle's RPM (or the laser's power).
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(int feedOverride READ feedOverride NOTIFY overridesChanged)
    Q_PROPERTY(int spindleOverride READ spindleOverride NOTIFY overridesChanged)
    Q_PROPERTY(QString feedText READ feedText NOTIFY overridesChanged)
    Q_PROPERTY(QString spindleText READ spindleText NOTIFY overridesChanged)
    Q_PROPERTY(bool showSpindleOverride READ showSpindleOverride NOTIFY changed)
    Q_PROPERTY(QString spindleLabel READ spindleLabel NOTIFY changed)  // "Spindle" / "Laser"

public:
    explicit JobModel(QObject* parent = nullptr);

    bool canStart() const;
    bool canPause() const;
    bool canStop() const;
    bool canPrepare() const;
    bool running() const;
    bool paused() const;
    bool showProgress() const;
    int received() const;
    int total() const;
    double percent() const;
    QString elapsed() const;
    QString remaining() const;
    QString sdFile() const;
    int totalLines() const;
    int lastLine() const;
    double defaultSafeHeight() const;
    QString units() const;
    bool connected() const;
    int feedOverride() const;
    int spindleOverride() const;
    QString feedText() const;
    QString spindleText() const;
    bool showSpindleOverride() const;
    QString spindleLabel() const;

    Q_INVOKABLE void start();  // resumes a paused or held job
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    // The outline above the stock: "" when it runs, else why not.
    Q_INVOKABLE QString outline();
    // Start from `line`, rising to `safeHeight` (workspace units) first.
    Q_INVOKABLE bool startFromLine(int line, double safeHeight);
    // 10-200 %, sent as the realtime override bytes.
    Q_INVOKABLE void setFeedOverride(int percent);
    Q_INVOKABLE void setSpindleOverride(int percent);

Q_SIGNALS:
    void changed();
    void progressChanged();
    void overridesChanged();
    void notice(const QString& text);

private:
    app::Machine& machine_;
};

}  // namespace gs::ui
