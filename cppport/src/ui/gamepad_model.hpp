#pragma once

// Tools > Gamepad (features/Gamepad) for QML: the profiles, one profile's
// button actions (with its lockout and 2nd-action buttons) and joystick
// options, adding a profile for the pad whose button was pressed, and a
// profile's import and export. Changes are saved as they are made. While
// the page is open the pads run nothing (upstream's holdListener): pressing
// buttons shows which they are.

#include "ui_model_base.hpp"

#include "gs/gamepad/profile.hpp"

#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

namespace gs::app {
class GamepadService;
}

namespace gs::ui {

class GamepadModel : public UiModelBase {
    Q_OBJECT
    QML_ELEMENT

    // This build reads gamepads (SDL3).
    Q_PROPERTY(bool available READ available CONSTANT)
    // [{index, name, connected}]
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    // The profile open, -1: the list.
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool padConnected READ padConnected NOTIFY padChanged)
    // [{value, label, primary, primaryTitle, secondary, secondaryTitle,
    // role ("lockout", "modifier" or "")}], the lockout and 2nd-action
    // buttons first.
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    // The open profile's pad: which buttons are down, the 2nd-action one
    // among them.
    Q_PROPERTY(QVariantList pressed READ pressed NOTIFY padChanged)
    Q_PROPERTY(bool modifierHeld READ modifierHeld NOTIFY padChanged)
    // Its sticks' axes, those under 0.4 as 0 (JoystickOptions' listener).
    Q_PROPERTY(QVariantList axes READ axes NOTIFY padChanged)
    // {stick1: {horizontal: {primaryAction, secondaryAction, isReversed},
    // vertical, mpgMode}, stick2, zeroThreshold, movementDistanceOverride,
    // fixedSpeedMode}; axes "x", "y", "z", "a" or "".
    Q_PROPERTY(QVariantMap joystick READ joystick NOTIFY changed)
    // Adding a profile: the pad whose button was pressed last, and whether
    // it has a profile already ("available", "exists"; "" before a press).
    Q_PROPERTY(QString detectedId READ detectedId NOTIFY detectChanged)
    Q_PROPERTY(QString detectedState READ detectedState NOTIFY detectChanged)
    // What a button can do: [{category, actions: [{id, title}]}] in
    // upstream's category order (macros included).
    Q_PROPERTY(QVariantList actionCategories READ actionCategories NOTIFY changed)

public:
    explicit GamepadModel(QObject* parent = nullptr);
    ~GamepadModel() override;

    bool available() const;
    QVariantList profiles() const;
    int current() const noexcept { return current_; }
    void setCurrent(int index);
    QString name() const;
    bool padConnected() const;
    QVariantList buttons() const;
    QVariantList pressed() const;
    bool modifierHeld() const;
    QVariantList axes() const;
    QVariantMap joystick() const;
    QString detectedId() const { return detectedId_; }
    QString detectedState() const { return detectedState_; }
    QVariantList actionCategories() const;

    // Adding: forget the pad detected; add a profile for it (named `name`,
    // else its id). False when there is none or it has a profile.
    Q_INVOKABLE void resetDetection();
    Q_INVOKABLE bool addProfile(const QString& name);
    Q_INVOKABLE void removeProfile(int index);
    // The open profile.
    Q_INVOKABLE void rename(const QString& name);
    Q_INVOKABLE void setAction(int button, bool secondary, const QString& id);
    Q_INVOKABLE void clearAction(int button, bool secondary);
    // A button as the lockout or the 2nd-action one (not both at once).
    Q_INVOKABLE void setLockout(int button, bool on);
    Q_INVOKABLE void setModifier(int button, bool on);
    Q_INVOKABLE void setLabel(int button, const QString& label);
    // stick "stick1"/"stick2", direction "horizontal"/"vertical"/"mpgMode",
    // field "primaryAction"/"secondaryAction" (an axis or "") or "isReversed".
    Q_INVOKABLE void setStick(const QString& stick, const QString& direction, const QString& field,
                              const QVariant& value);
    // "zeroThreshold" (0-99 %), "movementDistanceOverride" (0.001-99999 %),
    // "fixedSpeedMode".
    Q_INVOKABLE void setOption(const QString& key, const QVariant& value);
    Q_INVOKABLE QString actionTitle(const QString& id) const;
    // {ok, message}
    Q_INVOKABLE QVariantMap importFile(const QString& file);
    Q_INVOKABLE QVariantMap exportFile(const QString& file);
    Q_INVOKABLE QString exportName() const;

Q_SIGNALS:
    void padChanged();
    void detectChanged();

private:
    const gamepad::Profile* profile() const;
    // Applies `edit` to the open profile and saves.
    template <typename Edit>
    void editProfile(Edit&& edit);
    void save(std::vector<gamepad::Profile> profiles);
    void onButton(int index, int button, bool pressed);

    app::GamepadService& service_;
    int current_ = -1;
    QString detectedId_;
    QString detectedState_;
};

}  // namespace gs::ui
