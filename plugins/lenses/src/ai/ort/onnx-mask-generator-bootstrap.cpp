#include "ai/ort/onnx-mask-generator-bootstrap.hpp"

#if defined(LENSES_ENABLE_ORT)

#include "ai/ort/onnx-mask-generator-artifacts.hpp"
#include "ai/ort/onnx-mask-generator-provider.hpp"

#include <obs-module.h>

namespace lenses::ai::ort::detail {

bool CreateOrtSession(CreateOrtSessionContext &context, CreateOrtSessionResult &result,
		      std::string &error_out)
{
	if (!context.config || !context.env || !context.session || !context.log_callback) {
		error_out = "invalid ORT session bootstrap context";
		return false;
	}

	const auto &config = *context.config;
	result = {};

	try {
		*context.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "lenses",
							  context.log_callback,
							  context.log_callback_user_data);
		Ort::SessionOptions options;
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		if (config.profiling_enabled) {
			const std::string profile_prefix = BuildOrtProfilePrefix();
			options.EnableProfiling(profile_prefix.c_str());
		}

		ProviderSetupResult provider_setup = ConfigureExecutionProviders(options, config);
		if (!provider_setup.success) {
			error_out = provider_setup.error_detail.empty()
					    ? "execution provider setup failed"
					    : provider_setup.error_detail;
			context.session->reset();
			context.env->reset();
			return false;
		}
		result.configured_intra_spinning = provider_setup.configured_intra_spinning;
		result.configured_inter_spinning = provider_setup.configured_inter_spinning;
		result.active_execution_provider = provider_setup.active_execution_provider;
		result.coreml_requested = provider_setup.coreml_requested;
		result.coreml_enabled = provider_setup.coreml_enabled;
		result.cpu_ep_fallback_disabled = provider_setup.cpu_ep_fallback_disabled;

		try {
			const std::string optimized_model_path = BuildOrtOptimizedModelPath(config);
			if (!optimized_model_path.empty()) {
				options.SetOptimizedModelFilePath(optimized_model_path.c_str());
				blog(LOG_INFO, "[lenses] ORT optimized model path: %s",
				     optimized_model_path.c_str());
			}
		} catch (const Ort::Exception &ex) {
			blog(LOG_WARNING, "[lenses] Failed to set ORT optimized model path: %s",
			     ex.what());
		}

		*context.session =
			std::make_unique<Ort::Session>(**context.env, config.model_path.c_str(), options);
		error_out.clear();
		return true;
	} catch (const Ort::Exception &ex) {
		context.session->reset();
		context.env->reset();
		error_out = std::string("ORT exception: ") + ex.what();
		return false;
	} catch (const std::exception &ex) {
		context.session->reset();
		context.env->reset();
		error_out = std::string("std::exception: ") + ex.what();
		return false;
	} catch (...) {
		context.session->reset();
		context.env->reset();
		error_out = "unknown exception while creating ORT session";
		return false;
	}
}

} // namespace lenses::ai::ort::detail

#endif
