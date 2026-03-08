#pragma once

#include "lenses/core/interfaces.hpp"

namespace lenses::core {

class NoopCompositor final : public ICompositor {
public:
	ComposeResult Compose(const ComposeRequest &request, const ExecutionPlan &plan,
			     const MaskFrame *mask_frame) override;
};

} // namespace lenses::core
