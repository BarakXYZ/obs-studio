#include "lenses/ai/runtime/mask-generator-factory.hpp"

#include "lenses/ai/runtime/backend-resolver.hpp"
#include "lenses/ai/cloud/cloud-mask-generator.hpp"
#include "ai/ort/onnx-mask-generator.hpp"
#include "lenses/core/noop-mask-generator.hpp"

#include <obs-module.h>

#include <memory>
#include <string>

namespace lenses::ai::runtime {

MaskGeneratorFactoryResult CreateMaskGeneratorWithSelection(
	const lenses::core::RuntimeConfig &config)
{
	MaskGeneratorFactoryResult result{};
	result.selection = ResolveBackendSelection(config);
	result.resolved_config = config;
	result.resolved_config.provider = result.selection.provider;
	result.resolved_config.execution_provider = result.selection.execution_provider;

	std::string chain;
	for (size_t i = 0; i < result.selection.attempted_chain.size(); ++i) {
		if (i > 0)
			chain += " -> ";
		chain += result.selection.attempted_chain[i];
	}
	if (chain.empty())
		chain = "<none>";
	const int fallback_used = result.selection.used_fallback ? 1 : 0;
	const int supported = result.selection.supported ? 1 : 0;
	blog(LOG_INFO,
	     "[lenses] runtime backend selection provider='%s' execution_provider='%s' supported=%d fallback=%d chain=%s reason='%s'",
	     result.resolved_config.provider.c_str(),
	     result.resolved_config.execution_provider.c_str(), supported, fallback_used,
	     chain.c_str(), result.selection.reason.c_str());

	if (!result.selection.supported) {
		result.error = result.selection.reason.empty()
				       ? "backend selection failed"
				       : result.selection.reason;
		return result;
	}

#if defined(LENSES_ENABLE_CLOUD)
	if (result.selection.kind == BackendKind::Cloud) {
		auto local_fallback = std::make_unique<lenses::ai::ort::OnnxMaskGenerator>();
		result.generator =
			std::make_unique<lenses::ai::cloud::CloudMaskGenerator>(std::move(local_fallback));
		return result;
	}
#endif

	if (result.selection.kind == BackendKind::Ort) {
		result.generator = std::make_unique<lenses::ai::ort::OnnxMaskGenerator>();
		return result;
	}

	if (config.strict_runtime_checks) {
		result.error = "unsupported backend selection kind for strict runtime mode";
		return result;
	}

	result.generator = std::make_unique<lenses::core::NoopMaskGenerator>();
	return result;
}

std::unique_ptr<lenses::core::IMaskGenerator> CreateMaskGenerator(
	const lenses::core::RuntimeConfig &config)
{
	return CreateMaskGeneratorWithSelection(config).generator;
}

} // namespace lenses::ai::runtime
