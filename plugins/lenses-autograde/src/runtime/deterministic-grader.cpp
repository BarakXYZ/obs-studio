#include "runtime/deterministic-grader.hpp"

#include "runtime/deterministic/analysis.hpp"
#include "runtime/deterministic/lut.hpp"
#include "runtime/deterministic/params.hpp"

#include <algorithm>
#include <cstdio>

namespace lenses_autograde {
namespace {

using deterministic::GradeParams;
using deterministic::InputTransfer;
using deterministic::RoiAnalysis;

struct CandidateBuild {
	bool ok = false;
	InputTransfer transfer = InputTransfer::SrgbNonlinear;
	RoiAnalysis analysis{};
	GradeParams params{};
	std::vector<float> lut{};
	float mean_delta = 0.0f;
	float max_delta = 0.0f;
	float identity_mix = 1.0f;
	float score = -1e9f;
	std::string detail{};
};

const char *TransferHintLabel(DeterministicInputTransferHint hint)
{
	switch (hint) {
	case DeterministicInputTransferHint::Auto:
		return "auto";
	case DeterministicInputTransferHint::SrgbNonlinear:
		return "srgb_nl";
	case DeterministicInputTransferHint::Linear:
		return "linear";
	}
	return "unknown";
}

float CandidateScore(const GradeParams &params, float mean_delta, float max_delta)
{
	const float wb_dev =
		(std::fabs(params.wb_r - 1.0f) + std::fabs(params.wb_g - 1.0f) +
		 std::fabs(params.wb_b - 1.0f)) *
		(1.0f / 3.0f);
	const float exposure_dev = std::fabs(std::log2(std::max(params.exposure, 1e-3f)));
	const float contrast_dev = std::fabs(params.contrast - 1.0f);
	const float sat_dev = std::fabs(params.saturation - 1.0f);
	const float vib_dev = std::fabs(params.vibrance - 1.0f);

	return params.objective_score -
	       (0.95f * mean_delta + 0.40f * max_delta + 0.10f * exposure_dev + 0.08f * wb_dev +
		0.06f * contrast_dev + 0.05f * sat_dev + 0.04f * vib_dev);
}

bool BuildCandidateFromTransfer(const uint8_t *bgra, uint32_t width, uint32_t height,
				uint32_t linesize, uint32_t lut_dim, InputTransfer transfer,
				CandidateBuild &out_candidate)
{
	out_candidate = {};
	out_candidate.transfer = transfer;

	std::string analysis_detail;
	if (!deterministic::AnalyzeRoiFromBgra(bgra, width, height, linesize, transfer,
					      out_candidate.analysis, analysis_detail)) {
		out_candidate.detail = analysis_detail;
		return false;
	}

	std::string params_detail;
	if (!deterministic::SolveGradeParams(out_candidate.analysis, out_candidate.params,
					    params_detail)) {
		out_candidate.detail = params_detail;
		return false;
	}

	if (!deterministic::BuildLutFromParams(lut_dim, out_candidate.params, out_candidate.lut,
					 out_candidate.mean_delta,
					 out_candidate.max_delta)) {
		out_candidate.detail = "candidate: failed to build LUT";
		return false;
	}

	const float max_allowed_delta =
		deterministic::Lerp(0.10f, 0.18f, out_candidate.params.confidence);
	out_candidate.identity_mix = deterministic::ConstrainLutToIdentity(
		out_candidate.lut, lut_dim, max_allowed_delta, out_candidate.mean_delta,
		out_candidate.max_delta);
	out_candidate.params.detail_amount *= out_candidate.identity_mix;
	out_candidate.score = CandidateScore(out_candidate.params, out_candidate.mean_delta,
					     out_candidate.max_delta);
	out_candidate.ok = true;

	char detail[1024] = {};
	(void)snprintf(detail, sizeof(detail),
		       "%s | %s | score=%.3f delta=%.3f/%.3f mix=%.3f "
		       "conf=%.3f exp=%.3f ctr=%.3f sat=%.3f vib=%.3f wb=%.3f/%.3f/%.3f",
		       analysis_detail.c_str(), params_detail.c_str(), out_candidate.score,
		       out_candidate.mean_delta, out_candidate.max_delta, out_candidate.identity_mix,
		       out_candidate.params.confidence, out_candidate.params.exposure,
		       out_candidate.params.contrast, out_candidate.params.saturation,
		       out_candidate.params.vibrance, out_candidate.params.wb_r,
		       out_candidate.params.wb_g, out_candidate.params.wb_b);
	out_candidate.detail = detail;
	return true;
}

} // namespace

bool BuildDeterministicGradeFromBgra(const uint8_t *bgra, uint32_t width, uint32_t height,
				     uint32_t linesize, uint32_t lut_dim,
				     DeterministicInputTransferHint transfer_hint,
				     DeterministicGradeOutput &out_grade)
{
	out_grade = {};
	if (!bgra || width == 0 || height == 0 || linesize < width * 4U || lut_dim < 2U) {
		out_grade.detail = "Deterministic backend: invalid input frame";
		return false;
	}

	CandidateBuild srgb_candidate{};
	CandidateBuild linear_candidate{};
	bool srgb_ok = false;
	bool linear_ok = false;

	switch (transfer_hint) {
	case DeterministicInputTransferHint::Auto:
		srgb_ok = BuildCandidateFromTransfer(bgra, width, height, linesize, lut_dim,
					    InputTransfer::SrgbNonlinear, srgb_candidate);
		linear_ok = BuildCandidateFromTransfer(bgra, width, height, linesize, lut_dim,
					      InputTransfer::Linear, linear_candidate);
		break;
	case DeterministicInputTransferHint::SrgbNonlinear:
		srgb_ok = BuildCandidateFromTransfer(bgra, width, height, linesize, lut_dim,
					    InputTransfer::SrgbNonlinear, srgb_candidate);
		break;
	case DeterministicInputTransferHint::Linear:
		linear_ok = BuildCandidateFromTransfer(bgra, width, height, linesize, lut_dim,
					      InputTransfer::Linear, linear_candidate);
		break;
	}

	if (!srgb_ok && !linear_ok) {
		char detail[1024] = {};
		(void)snprintf(detail, sizeof(detail),
			       "Deterministic backend: no valid candidate (srgb=%s linear=%s)",
			       srgb_candidate.detail.empty() ? "n/a" : srgb_candidate.detail.c_str(),
			       linear_candidate.detail.empty() ? "n/a" : linear_candidate.detail.c_str());
		out_grade.detail = detail;
		return false;
	}

	const CandidateBuild *chosen = nullptr;
	const CandidateBuild *alternate = nullptr;
	if (srgb_ok && linear_ok) {
		if (linear_candidate.score > srgb_candidate.score) {
			chosen = &linear_candidate;
			alternate = &srgb_candidate;
		} else {
			chosen = &srgb_candidate;
			alternate = &linear_candidate;
		}
	} else if (srgb_ok) {
		chosen = &srgb_candidate;
	} else {
		chosen = &linear_candidate;
	}

	out_grade.lut = chosen->lut;
	out_grade.detail_amount = chosen->params.detail_amount;

	char detail[1200] = {};
	(void)snprintf(detail, sizeof(detail),
		       "Deterministic v8 (hint=%s input=%s score=%.3f alt=%s:%.3f "
		       "delta=%.3f/%.3f mix=%.3f conf=%.3f exp=%.3f ctr=%.3f sat=%.3f vib=%.3f "
		       "p05=%.3f p50=%.3f p95=%.3f spread=%.3f sh=%.3f hl=%.3f) %s",
		       TransferHintLabel(transfer_hint), deterministic::TransferLabel(chosen->transfer),
		       chosen->score,
		       alternate ? deterministic::TransferLabel(alternate->transfer) : "n/a",
		       alternate ? alternate->score : 0.0f, chosen->mean_delta, chosen->max_delta,
		       chosen->identity_mix, chosen->params.confidence, chosen->params.exposure,
		       chosen->params.contrast, chosen->params.saturation, chosen->params.vibrance,
		       chosen->params.p05, chosen->params.p50, chosen->params.p95,
		       chosen->params.spread, chosen->params.shadow_ratio,
		       chosen->params.highlight_ratio, chosen->detail.c_str());
	out_grade.detail = detail;
	return true;
}

} // namespace lenses_autograde
