#pragma once

// The jog speed presets and tap/hold jogging shared by the jog buttons and
// the keyboard shortcuts (gSender's Jogging widget state).

#include "gs/controller/jogging.hpp"

#include <QObject>

#include <array>
#include <memory>

namespace gs::app {

class Machine;

class Jogger final : public QObject {
    Q_OBJECT

public:
    explicit Jogger(Machine& machine, QObject* parent = nullptr);
    ~Jogger() override;

    controller::JogPreset preset() const noexcept { return preset_; }
    // Selecting a preset (again) loads its speeds from the settings.
    void selectPreset(controller::JogPreset preset);
    void cyclePreset();
    // In the workspace units: inch workspaces see the stored mm presets
    // converted (updateCurrentJogValues) and jog with G20.
    const controller::JogSpeeds& speeds() const noexcept { return speeds_; }
    bool metric() const noexcept { return metric_; }
    // Edited speeds, in use until a preset is selected.
    void setSpeeds(const controller::JogSpeeds& speeds);

    // Tap/hold jogging along `directions` (axis letter -> +1 or -1); the
    // distances come from the speeds (X and Y xyStep, Z zStep, A aStep).
    void press(const controller::JogAxes& directions);
    // The A buttons (AJog): A by the A step - in rotary mode the rotary, on
    // Y. Degrees are not lengths: these jogs go in G21 at the mm feed.
    // Deviation: upstream jogged the rotary's Y in the workspace units, 25.4x
    // too far in an inch workspace.
    void pressRotary(int direction);
    void release();
    bool isPressed() const;
    // canClickShortcut(): connected, no job running, idle or jogging.
    bool canJog() const;

Q_SIGNALS:
    void changed();

private:
    void rebuildHelper();
    controller::JogSpeeds presetSpeeds(controller::JogPreset preset) const;
    void stepJog(const controller::JogAxes& distances, double feedrate);
    void startContinuous(const controller::JogAxes& distances, double feedrate);
    void stopContinuous();

    Machine& machine_;
    controller::JogPreset preset_ = controller::JogPreset::Normal;
    controller::JogSpeeds speeds_;
    int threshold_ = 0;
    bool metric_ = true;
    // Rapid, Normal, Precise as last applied: a change reloads the selected preset.
    std::array<controller::JogSpeeds, 3> presets_{};
    bool rotaryJog_ = false;  // the jog in progress is A (or the rotary)
    std::unique_ptr<controller::JogHelper> helper_;
};

}  // namespace gs::app
