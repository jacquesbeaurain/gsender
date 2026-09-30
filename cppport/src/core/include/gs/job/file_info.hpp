#pragma once

// The texts of the file panel (features/FileControl: FileInformation's
// formatFileSize and Info's formatNumber and ranges) for a loaded program.

#include "gs/job/program_analysis.hpp"

#include <cstddef>
#include <string>

namespace gs::job {

// "512 Bytes", "12 KB", "3 MB".
std::string fileSizeText(std::size_t bytes);

// Integers as they are, else 2 decimals without trailing zeros.
std::string shortNumber(double value);

// The feed rates the file sets, in the workspace's units: "300 mm/min" or
// "100-500 in/min"; empty when it sets none.
std::string feedRangeText(const ProgramAnalysis& analysis, bool metric);

// The spindle speeds the file sets: "1000-12000 RPM"; empty when it sets none.
std::string speedRangeText(const ProgramAnalysis& analysis);

}  // namespace gs::job
