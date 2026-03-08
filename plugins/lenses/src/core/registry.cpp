#include "lenses/core/registry.hpp"

#include <memory>
#include <utility>

namespace lenses::core {

void MaskRegistry::Store(MaskFrame frame)
{
	std::scoped_lock lock(mutex_);
	latest_ = std::make_shared<MaskFrame>(std::move(frame));
}

std::shared_ptr<const MaskFrame> MaskRegistry::Latest() const
{
	std::scoped_lock lock(mutex_);
	return latest_;
}

void MaskRegistry::Clear()
{
	std::scoped_lock lock(mutex_);
	latest_.reset();
}

} // namespace lenses::core
