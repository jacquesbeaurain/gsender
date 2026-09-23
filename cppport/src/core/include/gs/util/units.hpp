#pragma once

// Workspace units (gSender's src/app/src/lib/units.ts and the jogging
// widget's convertValue): everything is stored in mm; inch workspaces see
// values converted, with upstream's rounding.

#include <string>

namespace gs::units {

double mm2in(double value);  // value / 25.4
double in2mm(double value);  // value * 25.4

// Number((value / 25.4).toFixed(3)) and Number((value * 25.4).toFixed(2)).
double convertToImperial(double value);
double convertToMetric(double value);

// Jogging's convertValue(): Number(converted.toFixed(precision)), converting
// with * (1 / 25.4) or * 25.4 when the units differ.
double convertValue(double value, bool fromMetric, bool toMetric, int precision = 3);

// mapPositionToUnits(): a machine position (mm) as the DRO shows it - mm
// with 2 decimals (a negative zero shown as 0.00), inches with 3; a custom
// number of decimals (workspace.customDecimalPlaces) when non-zero.
std::string positionText(double mm, bool metric, int customDecimals = 0);

}  // namespace gs::units
