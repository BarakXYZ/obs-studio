#include "ai/ort/onnx-mask-generator-provider.hpp"

#if defined(LENSES_ENABLE_ORT)

#include "ai/ort/onnx-mask-generator-artifacts.hpp"

#include <obs-module.h>

#if defined(__APPLE__) && __has_include(<onnxruntime/core/providers/coreml/coreml_provider_factory.h>)
#include <onnxruntime/core/providers/coreml/coreml_provider_factory.h>
#define LENSES_PROVIDER_HAS_ORT_COREML 1
#elif defined(__APPLE__) && __has_include(<coreml_provider_factory.h>)
#include <coreml_provider_factory.h>
#define LENSES_PROVIDER_HAS_ORT_COREML 1
#endif

#include <algorithm>
#include <thread>
#include <unordered_map>

namespace lenses::ai::ort::detail {

ProviderSetupResult ConfigureExecutionProviders(Ort::SessionOptions &options,
					const lenses::core::RuntimeConfig &config)
{
	ProviderSetupResult result{};
	const bool strict_runtime_checks = config.strict_runtime_checks;
	const bool wants_coreml = config.execution_provider.find("coreml") != std::string::npos;
	const bool wants_xnnpack = config.execution_provider.find("xnnpack") != std::string::npos;
	const bool require_static_coreml_inputs =
		config.model_static_input || !config.model_dynamic_shape;
	result.coreml_requested = wants_coreml;

	if (strict_runtime_checks) {
		try {
			/*
			 * ORT documented strict-runtime gate for heterogeneous EP flows:
			 * disable CPU EP fallback so unsupported nodes fail fast instead of
			 * silently executing on CPU.
			 */
			options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
			result.cpu_ep_fallback_disabled = true;
		} catch (const Ort::Exception &ex) {
			result.cpu_ep_fallback_disabled = false;
			blog(LOG_WARNING,
			     "[lenses] Failed to set ORT config session.disable_cpu_ep_fallback=1: %s",
			     ex.what());
		}
	}

	const bool offloaded_backend = wants_coreml || wants_xnnpack;
	const char *intra_spinning = offloaded_backend ? "0" : "1";
	const char *inter_spinning = offloaded_backend ? "0" : "1";
	result.configured_intra_spinning = intra_spinning;
	result.configured_inter_spinning = inter_spinning;
	try {
		options.AddConfigEntry("session.intra_op.allow_spinning", intra_spinning);
		options.AddConfigEntry("session.inter_op.allow_spinning", inter_spinning);
	} catch (const Ort::Exception &ex) {
		blog(LOG_WARNING,
		     "[lenses] Failed to set ORT spinning config entries: %s", ex.what());
	}

	const uint32_t intra_threads = config.cpu_intra_op_threads;
	const uint32_t inter_threads = std::max<uint32_t>(1, config.cpu_inter_op_threads);
	if (!wants_coreml && !wants_xnnpack) {
		options.SetIntraOpNumThreads((int)intra_threads);
		options.SetInterOpNumThreads((int)inter_threads);
	}

	auto try_enable_xnnpack = [&](std::string &detail_out) {
		try {
			std::unordered_map<std::string, std::string> provider_options;
			const uint32_t xnn_threads = config.cpu_intra_op_threads > 0
						 ? config.cpu_intra_op_threads
						 : std::max<uint32_t>(1, std::thread::hardware_concurrency());
			provider_options["intra_op_num_threads"] = std::to_string(xnn_threads);
			options.AppendExecutionProvider("XNNPACK", provider_options);
			/*
			 * XNNPACK manages its own worker pool. Keep ORT global pools minimal to
			 * avoid oversubscription on Apple Silicon.
			 */
			options.SetIntraOpNumThreads(1);
			options.SetInterOpNumThreads(1);
			detail_out = "xnnpack(" + std::to_string(xnn_threads) + " threads)";
			return true;
		} catch (const Ort::Exception &ex) {
			detail_out = ex.what();
		} catch (...) {
			detail_out = "unknown XNNPACK provider error";
		}
		return false;
	};

	bool coreml_ep_enabled = false;
	bool xnnpack_ep_enabled = false;
	std::string coreml_ep_detail;
	std::string xnnpack_ep_detail;

	if (wants_coreml) {
		/*
		 * Prefer provider-options path first so we can force modern CoreML tuning
		 * knobs (cache + fast specialization). Fall back to legacy flags only when
		 * this path is unavailable in the current ORT build.
		 */
		try {
			std::unordered_map<std::string, std::string> provider_options;
			provider_options["EnableOnSubgraphs"] = "1";
			provider_options["RequireStaticInputShapes"] =
				require_static_coreml_inputs ? "1" : "0";
			provider_options["SpecializationStrategy"] = "FastPrediction";
			provider_options["MLComputeUnits"] = "ALL";
			provider_options["ModelFormat"] = "MLProgram";
			if (config.profiling_enabled)
				provider_options["ProfileComputePlan"] = "1";
			const std::string cache_dir = BuildCoreMLCacheDirectory();
			if (!cache_dir.empty())
				provider_options["ModelCacheDirectory"] = cache_dir;
			options.AppendExecutionProvider("CoreML", provider_options);
			coreml_ep_enabled = true;
			result.active_execution_provider = "coreml";
			coreml_ep_detail =
				"provider-options(require_static_input_shapes=" +
				std::string(require_static_coreml_inputs ? "1" : "0") +
				",model_format=MLProgram,specialization=FastPrediction)";
		} catch (const Ort::Exception &ex) {
			coreml_ep_detail = ex.what();
		}

		if (!coreml_ep_enabled) {
#if defined(LENSES_PROVIDER_HAS_ORT_COREML)
			try {
				uint32_t coreml_flags = 0;
				coreml_flags |= COREML_FLAG_ENABLE_ON_SUBGRAPH;
#ifdef COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES
				if (require_static_coreml_inputs)
					coreml_flags |= COREML_FLAG_ONLY_ALLOW_STATIC_INPUT_SHAPES;
#endif
#ifdef COREML_FLAG_CREATE_MLPROGRAM
				coreml_flags |= COREML_FLAG_CREATE_MLPROGRAM;
#endif
				Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CoreML(options,
									 coreml_flags));
				coreml_ep_enabled = true;
				result.active_execution_provider = "coreml";
				coreml_ep_detail =
					"legacy-coreml-flags(require_static_input_shapes=" +
					std::string(require_static_coreml_inputs ? "1" : "0") + ")";
			} catch (const Ort::Exception &ex) {
				coreml_ep_detail = ex.what();
			}
#endif
		}

		if (!coreml_ep_enabled) {
			if (strict_runtime_checks) {
				result.success = false;
				result.error_detail =
					"CoreML provider unavailable in strict runtime mode. detail=" +
					(coreml_ep_detail.empty() ? std::string("unknown")
								 : coreml_ep_detail);
				blog(LOG_ERROR, "[lenses] %s", result.error_detail.c_str());
				result.coreml_enabled = false;
				result.xnnpack_enabled = false;
				return result;
			}
			if (try_enable_xnnpack(xnnpack_ep_detail)) {
				xnnpack_ep_enabled = true;
				result.active_execution_provider = "xnnpack";
				result.used_fallback = true;
				blog(LOG_WARNING,
				     "[lenses] CoreML provider unavailable; falling back to XNNPACK. coreml_detail='%s' xnnpack_detail='%s'",
				     coreml_ep_detail.empty() ? "unknown" : coreml_ep_detail.c_str(),
				     xnnpack_ep_detail.empty() ? "ok" : xnnpack_ep_detail.c_str());
			} else {
				result.used_fallback = true;
				blog(LOG_WARNING,
				     "[lenses] CoreML provider unavailable; continuing with ORT default providers. coreml_detail='%s' xnnpack_detail='%s'",
				     coreml_ep_detail.empty() ? "unknown" : coreml_ep_detail.c_str(),
				     xnnpack_ep_detail.empty() ? "not requested" : xnnpack_ep_detail.c_str());
			}
		} else {
			blog(LOG_INFO,
			     "[lenses] CoreML provider enabled via %s options={enable_on_subgraphs=1 require_static_input_shapes=%d model_dynamic_shape=%d model_static_input=%d model_static_output=%d}",
			     coreml_ep_detail.c_str(), require_static_coreml_inputs ? 1 : 0,
			     config.model_dynamic_shape ? 1 : 0, config.model_static_input ? 1 : 0,
			     config.model_static_output ? 1 : 0);
		}
	}

	if (!coreml_ep_enabled && wants_xnnpack && !xnnpack_ep_enabled) {
		if (try_enable_xnnpack(xnnpack_ep_detail)) {
			xnnpack_ep_enabled = true;
			result.active_execution_provider = "xnnpack";
			blog(LOG_INFO, "[lenses] XNNPACK provider enabled (%s)",
			     xnnpack_ep_detail.c_str());
		} else {
			if (strict_runtime_checks) {
				result.success = false;
				result.error_detail =
					"XNNPACK provider unavailable in strict runtime mode. detail=" +
					(xnnpack_ep_detail.empty() ? std::string("unknown")
								   : xnnpack_ep_detail);
				blog(LOG_ERROR, "[lenses] %s", result.error_detail.c_str());
				result.coreml_enabled = coreml_ep_enabled;
				result.xnnpack_enabled = false;
				return result;
			}
			result.used_fallback = true;
			blog(LOG_WARNING,
			     "[lenses] XNNPACK provider unavailable; using ORT default providers. detail='%s'",
			     xnnpack_ep_detail.empty() ? "unknown" : xnnpack_ep_detail.c_str());
		}
	}

	result.coreml_enabled = coreml_ep_enabled;
	result.xnnpack_enabled = xnnpack_ep_enabled;
	return result;
}

const char *CpuThreadingModeDescription(const std::string &execution_provider)
{
	if (execution_provider.find("coreml") != std::string::npos)
		return "offloaded(coreml)";
	if (execution_provider.find("xnnpack") != std::string::npos)
		return "offloaded(xnnpack)";
	return "default";
}

void LogSessionOptionsSummary(const lenses::core::RuntimeConfig &config,
			      const std::string &configured_intra_spinning,
			      const std::string &configured_inter_spinning,
			      bool cpu_ep_fallback_disabled)
{
	blog(LOG_INFO,
	     "[lenses] ORT session options graph_opt=all provider=%s cpu_threading=%s spinning(intra=%s,inter=%s) strict_runtime_checks=%d disable_cpu_ep_fallback=%d",
	     config.execution_provider.c_str(),
	     CpuThreadingModeDescription(config.execution_provider),
	     configured_intra_spinning.c_str(), configured_inter_spinning.c_str(),
	     config.strict_runtime_checks ? 1 : 0, cpu_ep_fallback_disabled ? 1 : 0);
}

} // namespace lenses::ai::ort::detail

#endif
