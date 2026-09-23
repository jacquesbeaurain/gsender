#include "gs/util/units.hpp"

#include "gs/util/jsnumber.hpp"

namespace gs::units {
namespace {

constexpr double kInch = 25.4;

double fixed(double value, int digits) {
    return js::stringToNumber(js::toFixed(value, digits));
}

}  // namespace

double mm2in(double value) {
    return value / kInch;
}

double in2mm(double value) {
    return value * kInch;
}

double convertToImperial(double value) {
    return fixed(value / kInch, 3);
}

double convertToMetric(double value) {
    return fixed(value * kInch, 2);
}

double convertValue(double value, bool fromMetric, bool toMetric, int precision) {
    if (fromMetric == toMetric) {
        return fixed(value, precision);
    }
    return fixed(fromMetric ? value * (1 / kInch) : value * kInch, precision);
}

std::string positionText(double mm, bool metric, int customDecimals) {
    if (!metric) {
        // setDecimalPlacesValue(): no negative-zero correction here.
        return js::toFixed(mm2in(mm), customDecimals == 0 ? 3 : customDecimals);
    }
    // setDecimalPlacesPosition() / determineCorrectedValue().
    const int decimals = customDecimals == 0 ? 2 : customDecimals;
    const std::string rounded = js::toFixed(mm, decimals);
    return js::stringToNumber(rounded) == 0 ? js::toFixed(0, decimals) : rounded;
}

}  // namespace gs::units
