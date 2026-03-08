#include "lenses/ai/runtime/model-registry.hpp"

namespace lenses::ai::runtime {

void ModelRegistry::Register(ModelDescriptor descriptor)
{
	if (descriptor.id.empty())
		descriptor.id = descriptor.model_path;

	std::scoped_lock lock(mutex_);
	descriptors_[descriptor.id] = std::move(descriptor);
}

std::optional<ModelDescriptor> ModelRegistry::FindById(const std::string &id) const
{
	std::scoped_lock lock(mutex_);
	auto it = descriptors_.find(id);
	if (it == descriptors_.end())
		return std::nullopt;
	return it->second;
}

std::optional<ModelDescriptor> ModelRegistry::FindByPath(const std::string &path) const
{
	std::scoped_lock lock(mutex_);
	for (const auto &entry : descriptors_) {
		if (entry.second.model_path == path)
			return entry.second;
	}
	return std::nullopt;
}

} // namespace lenses::ai::runtime
