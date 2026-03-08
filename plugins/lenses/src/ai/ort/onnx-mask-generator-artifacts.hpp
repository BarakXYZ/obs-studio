#pragma once

#include "lenses/core/interfaces.hpp"

#include <cstdint>
#include <string>

namespace lenses::ai::ort::detail {

std::string BuildCoreMLCacheDirectory();

std::string BuildOrtProfilePrefix();

uint64_t HashConfigForOptimizedArtifact(const lenses::core::RuntimeConfig &config);

std::string BuildOrtOptimizedModelPath(const lenses::core::RuntimeConfig &config);

} // namespace lenses::ai::ort::detail
