#pragma once

#include "lenses/core/interfaces.hpp"
#include "lenses/ai/runtime/backend-resolver.hpp"

#include <memory>
#include <string>

namespace lenses::ai::runtime {

struct MaskGeneratorFactoryResult {
	std::unique_ptr<lenses::core::IMaskGenerator> generator;
	lenses::core::RuntimeConfig resolved_config{};
	BackendSelection selection{};
	std::string error;
};

MaskGeneratorFactoryResult CreateMaskGeneratorWithSelection(
	const lenses::core::RuntimeConfig &config);
std::unique_ptr<lenses::core::IMaskGenerator> CreateMaskGenerator(const lenses::core::RuntimeConfig &config);

} // namespace lenses::ai::runtime
