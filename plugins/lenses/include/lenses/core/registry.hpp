#pragma once

#include "lenses/core/types.hpp"

#include <mutex>
#include <optional>
#include <memory>

namespace lenses::core {

class MaskRegistry final {
public:
	void Store(MaskFrame frame);
	[[nodiscard]] std::shared_ptr<const MaskFrame> Latest() const;
	void Clear();

private:
	mutable std::mutex mutex_;
	std::shared_ptr<MaskFrame> latest_;
};

} // namespace lenses::core
