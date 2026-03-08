#include "ai/ort/onnx-mask-generator-artifacts.hpp"

#include <obs-module.h>
#include <util/bmem.h>
#include <util/platform.h>

#include <inttypes.h>
#include <sys/stat.h>
#include <time.h>
#include <string>

namespace lenses::ai::ort::detail {

namespace {

constexpr uint64_t kOrtOptimizedArtifactSchemaVersion = 2ULL;

char *ResolveLensesConfigPath(const char *relative_path)
{
	obs_module_t *module = obs_current_module();
	if (module)
		return obs_module_get_config_path(module, relative_path);

	std::string fallback = "plugin_config/lenses/";
	if (relative_path && *relative_path)
		fallback += relative_path;
	return os_get_config_path_ptr(fallback.c_str());
}

} // namespace

std::string BuildCoreMLCacheDirectory()
{
	char *cache_path = ResolveLensesConfigPath("coreml-cache");
	if (!cache_path)
		return {};
	const std::string out(cache_path);
	(void)os_mkdirs(cache_path);
	bfree(cache_path);
	return out;
}

std::string BuildOrtProfilePrefix()
{
	char *profile_dir = ResolveLensesConfigPath("ort-profiles");
	if (!profile_dir)
		return "lenses-ort-profile";
	(void)os_mkdirs(profile_dir);
	std::string prefix(profile_dir);
	prefix += "/lenses-ort";
	bfree(profile_dir);
	return prefix;
}

uint64_t HashConfigForOptimizedArtifact(const lenses::core::RuntimeConfig &config)
{
	uint64_t hash = 1469598103934665603ULL;
	auto hash_bytes = [&hash](const void *bytes, size_t size) {
		const uint8_t *cursor = static_cast<const uint8_t *>(bytes);
		for (size_t i = 0; i < size; ++i) {
			hash ^= (uint64_t)cursor[i];
			hash *= 1099511628211ULL;
		}
	};
	auto hash_cstr = [&hash_bytes](const std::string &value) {
		hash_bytes(value.data(), value.size());
	};

	auto hash_model_file_identity = [&](const std::string &path) {
		struct stat st = {};
		if (path.empty() || stat(path.c_str(), &st) != 0)
			return;

		const uint64_t size = (uint64_t)st.st_size;
		hash_bytes(&size, sizeof(size));

		const int64_t modified_sec = (int64_t)st.st_mtime;
		hash_bytes(&modified_sec, sizeof(modified_sec));
#if defined(__APPLE__) || defined(__linux__)
#if defined(__APPLE__)
		const int64_t modified_nsec = (int64_t)st.st_mtimespec.tv_nsec;
#else
		const int64_t modified_nsec = (int64_t)st.st_mtim.tv_nsec;
#endif
		hash_bytes(&modified_nsec, sizeof(modified_nsec));
#endif
	};

	hash_bytes(&kOrtOptimizedArtifactSchemaVersion, sizeof(kOrtOptimizedArtifactSchemaVersion));
	hash_cstr(config.model_path);
	hash_model_file_identity(config.model_path);
	hash_cstr(config.execution_provider);
	hash_bytes(&config.input_width, sizeof(config.input_width));
	hash_bytes(&config.input_height, sizeof(config.input_height));
	hash_bytes(&config.enable_iobinding, sizeof(config.enable_iobinding));
	hash_bytes(&config.strict_runtime_checks, sizeof(config.strict_runtime_checks));
	hash_bytes(&config.profiling_enabled, sizeof(config.profiling_enabled));
	return hash;
}

std::string BuildOrtOptimizedModelPath(const lenses::core::RuntimeConfig &config)
{
	char *optimized_dir = ResolveLensesConfigPath("ort-optimized");
	if (!optimized_dir)
		return {};

	const std::string directory(optimized_dir);
	(void)os_mkdirs(optimized_dir);
	bfree(optimized_dir);
	if (directory.empty())
		return {};

	const uint64_t hash = HashConfigForOptimizedArtifact(config);
	char filename[64] = {0};
	snprintf(filename, sizeof(filename), "%016" PRIx64 ".ort", hash);
	return directory + "/" + filename;
}

} // namespace lenses::ai::ort::detail
