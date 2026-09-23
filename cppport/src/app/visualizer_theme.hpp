#pragma once

// The visualizer's colour schemes (widgets.visualizer.theme): gviewer's
// presets as gSender builds them (Visualizer/viewerTheme.ts) - its "Dark"
// with the Workshop High-Contrast colours, the others with gSender's
// bounding box and machine bed colours.

#include <QColor>
#include <QStringList>

namespace gs::app {

struct VisualizerTheme {
    QColor background;
    QColor gridMajor;
    QColor gridMinor;
    QColor axisX;
    QColor axisY;
    QColor axisZ;
    QColor rapid;  // drawn at 30 % (rapidOpacity)
    QColor cutting;
    QColor processed;
    QColor boundingBox;  // drawn at 65 %
    QColor machineBed;
    QColor keepout;
    QColor tool;
    QColor text;  // captions and the view buttons
    bool light = false;
};

// "Dark", "Light", "Flexoki Dark", "Tokyo Night", "Gruvbox Light",
// "Ayu Dark", "Ayu Light".
QStringList visualizerThemeNames();
// The theme by name; "Dark" for an unknown one.
const VisualizerTheme& visualizerTheme(const QString& name);

}  // namespace gs::app
