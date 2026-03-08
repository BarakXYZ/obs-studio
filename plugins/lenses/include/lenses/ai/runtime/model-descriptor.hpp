#pragma once

#include <string>
#include <vector>

namespace lenses::ai::runtime {

struct ModelDescriptor {
	std::string id;
	std::string model_path;
	std::string provider;
	std::vector<std::string> class_names;
	uint32_t input_width = 0;
	uint32_t input_height = 0;
	bool static_input = false;
	bool static_output = false;
	bool supports_iobinding_static_outputs = false;
	std::vector<uint32_t> recommended_input_sizes;
	float confidence_threshold = 0.25f;
};

} // namespace lenses::ai::runtime
