#pragma once

// Rectangular grid capture with the 3D probe (the feature/probe-mesh-capture
// branch's MeshProbe.tsx and useMeshCapture.ts, a "mesh" there): the grid's
// spacing and point counts, the run with its progress and graceful Stop, and
// the points saved as a CSV. The run itself is the core's probe::GridCapture,
// fed the board's lines.

#include "ui_model_base.hpp"

#include "gs/probe/grid_capture.hpp"

#include <QString>
#include <QTimer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>

namespace gs::ui {

class GridCaptureModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    // "idle", "running", "done", "stopped", "failed".
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    // The run's progress, as the dialog's status line puts it.
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int captured READ captured NOTIFY changed)
    // The points held for the CSV.
    Q_PROPERTY(int pointCount READ pointCount NOTIFY changed)
    // The last point captured, in the workspace units ("" before any).
    Q_PROPERTY(QString lastPoint READ lastPoint NOTIFY changed)
    Q_PROPERTY(bool simulated READ simulated NOTIFY changed)

public:
    explicit GridCaptureModel(QObject* parent = nullptr);

    QString status() const;
    bool running() const { return capture_.running(); }
    QString statusText() const;
    int total() const { return capture_.total(); }
    int captured() const { return capture_.captured(); }
    int pointCount() const { return static_cast<int>(capture_.points().size()); }
    QString lastPoint() const;
    bool simulated() const;

    // Opening the dialog: on the simulator, a gently sloping surface goes
    // under the probe (the operator's part).
    Q_INVOKABLE void begin();
    // Spacing in the workspace units, the point counts (1 to 200 each).
    // False when it cannot run (not idle, a run on, bad numbers).
    Q_INVOKABLE bool startGrid(const QString& dx, const QString& nx, const QString& dy, const QString& ny);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clear();
    // The CSV (X,Y,Z, workspace units), and writing it to a file or file URL.
    Q_INVOKABLE QString csv() const;
    Q_INVOKABLE QVariantMap save(const QString& file);
    Q_INVOKABLE QString defaultFileName() const;

    // A probe move is short, but the rapid before it can cross the whole
    // table: this long without a result fails the run.
    static constexpr int kProbeTimeoutMs = 120000;
    void setProbeTimeout(int ms) { timer_.setInterval(ms); }

private:
    std::optional<probe::CaptureSetup> setup() const;
    void send(std::vector<std::string> lines);

    probe::GridCapture capture_;
    QTimer timer_;
};

}  // namespace gs::ui
