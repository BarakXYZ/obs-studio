#pragma once

#include "lenses/core/interfaces.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace lenses::ai::runtime {

enum class BackendKind : uint8_t {
	Noop = 0,
	Ort = 1,
	Cloud = 2,
};

struct BackendSelection {
	BackendKind kind = BackendKind::Noop;
	std::string provider;
	std::string execution_provider;
	std::vector<std::string> attempted_chain;
	bool supported = true;
	bool used_fallback = false;
	std::string reason;
};

BackendSelection ResolveBackendSelection(const lenses::core::RuntimeConfig &requested);

} // namespace lenses::ai::runtime
