#include "lenses/ai/runtime/backend-resolver.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace lenses::ai::runtime {

namespace {

static inline std::string ToLower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		       [](unsigned char ch) { return (char)std::tolower(ch); });
	return value;
}

static inline bool StartsWith(const std::string &value, const std::string &prefix)
{
	if (value.size() < prefix.size())
		return false;
	return value.compare(0, prefix.size(), prefix) == 0;
}

static inline void SetOrtSelection(BackendSelection &selection, const std::string &provider)
{
	selection.kind = BackendKind::Ort;
	selection.provider = provider;
	selection.execution_provider = provider;
	selection.supported = true;
	selection.used_fallback = false;
}

static BackendSelection BuildUnsupportedSelection(const std::string &requested_provider,
						  const std::string &reason,
						  bool strict_runtime_checks)
{
	BackendSelection selection{};
	selection.kind = BackendKind::Noop;
	selection.provider = "noop";
	selection.execution_provider = "noop";
	selection.supported = !strict_runtime_checks;
	selection.used_fallback = !strict_runtime_checks;
	selection.attempted_chain.emplace_back(requested_provider.empty() ? "unknown"
								 : requested_provider);
	if (strict_runtime_checks) {
		selection.reason = reason + " (strict_runtime_checks=1)";
	} else {
		selection.reason = reason + "; using noop fallback";
	}
	return selection;
}

static BackendSelection ResolveAutoSelection(bool strict_runtime_checks)
{
	BackendSelection selection{};
	selection.provider = "auto";
	selection.execution_provider = "auto";
	selection.reason = "auto provider requested";

#if defined(LENSES_ENABLE_ORT)
	(void)strict_runtime_checks;
#endif

#if defined(__APPLE__)
	selection.attempted_chain.emplace_back("coreml-native (not enabled in current build)");
#endif
#if defined(_WIN32)
	selection.attempted_chain.emplace_back("winml (not enabled in current build)");
#endif

#if defined(LENSES_ENABLE_ORT)
#if defined(__APPLE__)
	selection.attempted_chain.emplace_back("ort-coreml");
	SetOrtSelection(selection, "ort-coreml");
	selection.reason = "selected ort-coreml from auto ladder";
	return selection;
#elif defined(_WIN32)
	selection.attempted_chain.emplace_back("ort-tensorrt (requires ORT TensorRT build)");
	selection.attempted_chain.emplace_back("ort-cuda (requires ORT CUDA build)");
	selection.attempted_chain.emplace_back("ort-cpu");
	SetOrtSelection(selection, "ort-cpu");
	selection.reason = "selected ort-cpu from auto ladder";
	return selection;
#else
	selection.attempted_chain.emplace_back("ort-tensorrt (requires ORT TensorRT build)");
	selection.attempted_chain.emplace_back("ort-openvino (requires ORT OpenVINO build)");
	selection.attempted_chain.emplace_back("ort-cpu");
	SetOrtSelection(selection, "ort-cpu");
	selection.reason = "selected ort-cpu from auto ladder";
	return selection;
#endif
#else
	selection.attempted_chain.emplace_back("ort backend unavailable (LENSES_ENABLE_ORT=OFF)");
	return BuildUnsupportedSelection("auto", "no inference backend compiled",
					 strict_runtime_checks);
#endif
}

} // namespace

BackendSelection ResolveBackendSelection(const lenses::core::RuntimeConfig &requested)
{
	BackendSelection selection{};
	const bool strict_runtime_checks = requested.strict_runtime_checks;

	const std::string provider = ToLower(requested.provider.empty() ? std::string("auto")
								      : requested.provider);
	const std::string execution_provider = ToLower(requested.execution_provider);

	if (StartsWith(provider, "cloud")) {
#if defined(LENSES_ENABLE_CLOUD)
		selection.kind = BackendKind::Cloud;
		selection.provider = provider;
		selection.execution_provider = execution_provider.empty() ? "ort-cpu" : execution_provider;
		selection.supported = true;
		selection.used_fallback = false;
		selection.attempted_chain.emplace_back("cloud");
		selection.reason = "cloud provider explicitly selected";
		return selection;
#else
		return BuildUnsupportedSelection(
			provider, "cloud backend requested but LENSES_ENABLE_CLOUD is disabled",
			strict_runtime_checks);
#endif
	}

	if (provider == "auto")
		return ResolveAutoSelection(strict_runtime_checks);

	if (provider == "noop") {
		selection.kind = BackendKind::Noop;
		selection.provider = "noop";
		selection.execution_provider = "noop";
		selection.supported = true;
		selection.used_fallback = false;
		selection.attempted_chain.emplace_back("noop");
		selection.reason = "noop provider explicitly selected";
		return selection;
	}

	if (provider == "coreml") {
#if defined(LENSES_ENABLE_ORT)
		SetOrtSelection(selection, "ort-coreml");
		selection.attempted_chain.emplace_back("coreml alias -> ort-coreml");
		selection.reason = "coreml alias normalized to ort-coreml";
#else
		return BuildUnsupportedSelection("coreml", "coreml alias requested but ORT is unavailable",
					 strict_runtime_checks);
#endif
		return selection;
	}

	if (provider == "cpu") {
#if defined(LENSES_ENABLE_ORT)
		SetOrtSelection(selection, "ort-cpu");
		selection.attempted_chain.emplace_back("cpu alias -> ort-cpu");
		selection.reason = "cpu alias normalized to ort-cpu";
#else
		return BuildUnsupportedSelection("cpu", "cpu alias requested but ORT is unavailable",
					 strict_runtime_checks);
#endif
		return selection;
	}

	if (StartsWith(provider, "ort")) {
#if defined(LENSES_ENABLE_ORT)
		SetOrtSelection(selection, provider);
		selection.execution_provider =
			execution_provider.empty() ? provider : execution_provider;
		selection.attempted_chain.emplace_back(provider);
		selection.reason = "explicit ORT provider selected";
#else
		return BuildUnsupportedSelection(provider,
					 provider + " requested but ORT is unavailable",
					 strict_runtime_checks);
#endif
		return selection;
	}

	return BuildUnsupportedSelection(provider, "unknown provider requested",
					 strict_runtime_checks);
}

} // namespace lenses::ai::runtime
