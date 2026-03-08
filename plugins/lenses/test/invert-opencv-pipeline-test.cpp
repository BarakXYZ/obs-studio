#include "filter/invert/lenses-invert-opencv-pipeline.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

using lenses::filter::invert::OpenCvRegionMaskPipeline;
using lenses::filter::invert::OpenCvRegionParams;
using lenses::filter::invert::OpenCvRegionStats;

bool Expect(bool condition, const std::string &message)
{
	if (condition)
		return true;
	std::cerr << "FAIL: " << message << std::endl;
	return false;
}

std::vector<uint8_t> MakeSolidBgra(uint32_t width, uint32_t height, uint8_t value)
{
	std::vector<uint8_t> frame((size_t)width * (size_t)height * 4U, 0);
	for (size_t i = 0; i < frame.size(); i += 4U) {
		frame[i + 0U] = value;
		frame[i + 1U] = value;
		frame[i + 2U] = value;
		frame[i + 3U] = 255U;
	}
	return frame;
}

void FillRect(std::vector<uint8_t> &frame, uint32_t width, uint32_t height, uint32_t x0,
	      uint32_t y0, uint32_t x1, uint32_t y1, uint8_t value)
{
	if (x0 >= x1 || y0 >= y1)
		return;
	x0 = x0 > width ? width : x0;
	x1 = x1 > width ? width : x1;
	y0 = y0 > height ? height : y0;
	y1 = y1 > height ? height : y1;
	for (uint32_t y = y0; y < y1; ++y) {
		for (uint32_t x = x0; x < x1; ++x) {
			const size_t index = ((size_t)y * (size_t)width + (size_t)x) * 4U;
			frame[index + 0U] = value;
			frame[index + 1U] = value;
			frame[index + 2U] = value;
			frame[index + 3U] = 255U;
		}
	}
}

void FillRectBgr(std::vector<uint8_t> &frame, uint32_t width, uint32_t height, uint32_t x0,
		 uint32_t y0, uint32_t x1, uint32_t y1, uint8_t b, uint8_t g, uint8_t r)
{
	if (x0 >= x1 || y0 >= y1)
		return;
	x0 = x0 > width ? width : x0;
	x1 = x1 > width ? width : x1;
	y0 = y0 > height ? height : y0;
	y1 = y1 > height ? height : y1;
	for (uint32_t y = y0; y < y1; ++y) {
		for (uint32_t x = x0; x < x1; ++x) {
			const size_t index = ((size_t)y * (size_t)width + (size_t)x) * 4U;
			frame[index + 0U] = b;
			frame[index + 1U] = g;
			frame[index + 2U] = r;
			frame[index + 3U] = 255U;
		}
	}
}

uint8_t MaskAt(const std::vector<uint8_t> &mask, uint32_t width, uint32_t x, uint32_t y)
{
	return mask[(size_t)y * (size_t)width + (size_t)x];
}

OpenCvRegionParams DefaultParams()
{
	OpenCvRegionParams params;
	params.threshold = 0.60f;
	params.softness = 0.45f;
	params.coverage = 0.35f;
	params.min_area_px = 96.0f;
	params.min_side_px = 8.0f;
	params.min_fill = 0.10f;
	params.min_coverage = 0.001f;
	return params;
}

bool TestLargeBrightRegionKeepsInteriorText()
{
	constexpr uint32_t width = 128;
	constexpr uint32_t height = 128;
	std::vector<uint8_t> frame = MakeSolidBgra(width, height, 22);

	FillRect(frame, width, height, 18, 18, 110, 110, 240);
	/* Simulated dark text strokes inside a bright card. */
	FillRect(frame, width, height, 48, 56, 84, 60, 16);
	FillRect(frame, width, height, 48, 68, 84, 72, 16);
	FillRect(frame, width, height, 56, 52, 60, 76, 16);

	OpenCvRegionMaskPipeline pipeline;
	OpenCvRegionParams params = DefaultParams();
	OpenCvRegionStats stats{};
	std::vector<uint8_t> mask;
	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, params, mask, stats),
		    "pipeline should succeed for bright card")) {
		return false;
	}
	if (!Expect(mask.size() == (size_t)width * (size_t)height,
		    "mask dimensions should match input")) {
		return false;
	}

	if (!Expect(MaskAt(mask, width, 58, 58) > 200,
		    "dark text hole inside bright card should still be selected")) {
		return false;
	}
	if (!Expect(MaskAt(mask, width, 8, 8) < 20,
		    "dark background outside card should stay unselected")) {
		return false;
	}
	return Expect(stats.accepted_pixels > 5000,
		      "large bright card should produce substantial selected area");
}

bool TestTinyBrightTextRejected()
{
	constexpr uint32_t width = 128;
	constexpr uint32_t height = 128;
	std::vector<uint8_t> frame = MakeSolidBgra(width, height, 18);

	/* Tiny white glyph strokes on a dark background. */
	FillRect(frame, width, height, 60, 54, 62, 74, 245);
	FillRect(frame, width, height, 56, 56, 66, 58, 245);
	FillRect(frame, width, height, 56, 70, 66, 72, 245);

	OpenCvRegionMaskPipeline pipeline;
	OpenCvRegionParams params = DefaultParams();
	params.min_area_px = 120.0f;
	params.min_side_px = 10.0f;
	OpenCvRegionStats stats{};
	std::vector<uint8_t> mask;
	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, params, mask, stats),
		    "pipeline should succeed for tiny glyph test")) {
		return false;
	}

	if (!Expect(MaskAt(mask, width, 60, 60) < 20,
		    "tiny bright text should be rejected by component gate")) {
		return false;
	}
	return Expect(stats.accepted_pixels < 32,
		      "accepted area should remain near zero for tiny text-only highlights");
}

bool TestCoverageParameterTightensMask()
{
	constexpr uint32_t width = 128;
	constexpr uint32_t height = 128;
	std::vector<uint8_t> frame = MakeSolidBgra(width, height, 22);
	FillRect(frame, width, height, 20, 20, 108, 108, 240);

	OpenCvRegionMaskPipeline pipeline;
	OpenCvRegionStats loose_stats{};
	OpenCvRegionStats tight_stats{};
	std::vector<uint8_t> loose_mask;
	std::vector<uint8_t> tight_mask;

	OpenCvRegionParams loose = DefaultParams();
	loose.coverage = 0.10f;
	OpenCvRegionParams tight = DefaultParams();
	tight.coverage = 0.75f;

	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, loose, loose_mask,
		    loose_stats), "loose coverage run should succeed")) {
		return false;
	}
	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, tight, tight_mask,
		    tight_stats), "tight coverage run should succeed")) {
		return false;
	}

	if (!Expect(loose_stats.accepted_pixels > tight_stats.accepted_pixels,
		    "higher coverage should reduce accepted area")) {
		return false;
	}
	return Expect(MaskAt(loose_mask, width, 22, 22) >= MaskAt(tight_mask, width, 22, 22),
		      "higher coverage should tighten edge alpha response");
}

bool TestHueQualifierExcludeModeSupportsMultipleRanges()
{
	constexpr uint32_t width = 160;
	constexpr uint32_t height = 96;
	std::vector<uint8_t> frame = MakeSolidBgra(width, height, 20);

	FillRectBgr(frame, width, height, 8, 12, 52, 84, 20, 20, 245);   // red-ish
	FillRectBgr(frame, width, height, 58, 12, 102, 84, 20, 245, 20); // green-ish
	FillRectBgr(frame, width, height, 108, 12, 152, 84, 245, 20, 20); // blue-ish

	OpenCvRegionMaskPipeline pipeline;
	OpenCvRegionParams params = DefaultParams();
	params.threshold = 0.08f;
	params.softness = 0.0f;
	params.coverage = 0.10f;
	params.hue_qualifier.enabled = 1;
	params.hue_qualifier.mode = LENSES_INVERT_HUE_RANGE_MODE_EXCLUDE;
	params.hue_qualifier.bands[0] = {1, 0.0f, 42.0f, 10.0f};     // red window
	params.hue_qualifier.bands[1] = {1, 240.0f, 42.0f, 10.0f};   // blue window

	OpenCvRegionStats stats{};
	std::vector<uint8_t> mask;
	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, params, mask, stats),
		    "exclude-mode hue qualifier run should succeed")) {
		return false;
	}

	if (!Expect(MaskAt(mask, width, 30, 44) < 25, "red range should be excluded")) {
		return false;
	}
	if (!Expect(MaskAt(mask, width, 130, 44) < 25, "blue range should be excluded")) {
		return false;
	}
	return Expect(MaskAt(mask, width, 80, 44) > 170,
		      "green region should remain selected in exclude mode");
}

bool TestHueQualifierIncludeModeSupportsMultipleRanges()
{
	constexpr uint32_t width = 160;
	constexpr uint32_t height = 96;
	std::vector<uint8_t> frame = MakeSolidBgra(width, height, 20);

	FillRectBgr(frame, width, height, 8, 12, 52, 84, 20, 20, 245);   // red-ish
	FillRectBgr(frame, width, height, 58, 12, 102, 84, 20, 245, 20); // green-ish
	FillRectBgr(frame, width, height, 108, 12, 152, 84, 245, 20, 20); // blue-ish

	OpenCvRegionMaskPipeline pipeline;
	OpenCvRegionParams params = DefaultParams();
	params.threshold = 0.08f;
	params.softness = 0.0f;
	params.coverage = 0.10f;
	params.hue_qualifier.enabled = 1;
	params.hue_qualifier.mode = LENSES_INVERT_HUE_RANGE_MODE_INCLUDE;
	params.hue_qualifier.bands[0] = {1, 0.0f, 44.0f, 12.0f};     // red window
	params.hue_qualifier.bands[1] = {1, 240.0f, 44.0f, 12.0f};   // blue window

	OpenCvRegionStats stats{};
	std::vector<uint8_t> mask;
	if (!Expect(pipeline.BuildMask(frame.data(), width, height, width * 4U, params, mask, stats),
		    "include-mode hue qualifier run should succeed")) {
		return false;
	}

	const uint8_t red_value = MaskAt(mask, width, 30, 44);
	const uint8_t blue_value = MaskAt(mask, width, 130, 44);
	const uint8_t green_value = MaskAt(mask, width, 80, 44);
	if (!Expect(red_value > 170,
		    "red range should be selected (value=" + std::to_string((int)red_value) + ")")) {
		return false;
	}
	if (!Expect(blue_value > 170,
		    "blue range should be selected (value=" + std::to_string((int)blue_value) + ")")) {
		return false;
	}
	return Expect(green_value < 30,
		      "green region should be rejected in include mode (value=" +
			      std::to_string((int)green_value) + ")");
}

} // namespace

int main()
{
	bool ok = true;
	ok = TestLargeBrightRegionKeepsInteriorText() && ok;
	ok = TestTinyBrightTextRejected() && ok;
	ok = TestCoverageParameterTightensMask() && ok;
	ok = TestHueQualifierExcludeModeSupportsMultipleRanges() && ok;
	ok = TestHueQualifierIncludeModeSupportsMultipleRanges() && ok;

	if (!ok)
		return 1;

	std::cout << "invert-opencv-pipeline-test: PASS" << std::endl;
	return 0;
}
