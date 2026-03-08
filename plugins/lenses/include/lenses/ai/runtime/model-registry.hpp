#pragma once

#include "lenses/ai/runtime/model-descriptor.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace lenses::ai::runtime {

class ModelRegistry final {
public:
	void Register(ModelDescriptor descriptor);
	std::optional<ModelDescriptor> FindById(const std::string &id) const;
	std::optional<ModelDescriptor> FindByPath(const std::string &path) const;

private:
	mutable std::mutex mutex_;
	std::unordered_map<std::string, ModelDescriptor> descriptors_;
};

} // namespace lenses::ai::runtime
