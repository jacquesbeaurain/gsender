#pragma once

// "Run outline" (gSender's src/app/src/workers/Outline.worker.ts): a program
// that traces the job's footprint above the stock - the hull of its toolpath
// (Detailed), its bounding box (Square), or the box of its cutting moves
// only (Rapidless Square). The lines use the controller's %assignments and
// [expressions] and are run with its gcode command in the loaded file's
// context. Output matches upstream (tests/data/outline_golden.json).

#include "gs/gcode/interpreter.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::job {

enum class OutlineMode { Detailed, Square, RapidlessSquare };
std::string_view outlineModeName(OutlineMode mode);  // "Detailed", "Square", "Rapidless Square"
std::optional<OutlineMode> outlineModeFromName(std::string_view name);

struct OutlineInput {
    OutlineMode mode = OutlineMode::Detailed;
    bool isLaser = false;     // traces with the laser at S1
    double zTravel = 5;       // lift before tracing (and back down after)
    double outlineSpeed = 0;  // workspace.outlineSpeed: > 0 traces with G1 at it
    // The toolpath's vertices, flat x,y,z (the visualizer's). Detailed takes
    // its hull; Square uses the file's [xmin]... when there are any.
    std::vector<float> vertices;
    gcode::BoundingBox bbox;  // Square without vertices
    std::string content;      // Rapidless Square: the program
};

// The worker's answer, or nullopt where the JavaScript throws (concaveman
// fails on a hull with no points).
std::optional<std::vector<std::string>> outlineProgram(const OutlineInput& input);

// robust-predicates' orient2d: the exact sign of (ay-cy)(bx-cx)-(ax-cx)(by-cy)
// for the given doubles (Shewchuk's adaptive precision); positive when a, b,
// c turn clockwise.
double orient2d(double ax, double ay, double bx, double by, double cx, double cy);

}  // namespace gs::job
