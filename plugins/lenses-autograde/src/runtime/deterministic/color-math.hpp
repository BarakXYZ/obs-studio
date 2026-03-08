#pragma once

#include <algorithm>
#include <cmath>

namespace lenses_autograde::deterministic {

constexpr float kEpsilon = 1e-6f;

struct Rgb {
	float r = 0.0f;
	float g = 0.0f;
	float b = 0.0f;
};

struct Oklab {
	float L = 0.0f;
	float a = 0.0f;
	float b = 0.0f;
};

inline float Clamp(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

inline float Clamp01(float v)
{
	return Clamp(v, 0.0f, 1.0f);
}

inline float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}

inline float SmoothStep(float edge0, float edge1, float x)
{
	if (edge1 <= edge0)
		return x >= edge1 ? 1.0f : 0.0f;
	const float t = Clamp01((x - edge0) / (edge1 - edge0));
	return t * t * (3.0f - 2.0f * t);
}

inline float SrgbToLinear(float u)
{
	u = Clamp01(u);
	if (u <= 0.04045f)
		return u * (1.0f / 12.92f);
	return std::pow((u + 0.055f) * (1.0f / 1.055f), 2.4f);
}

inline float LinearToSrgb(float u)
{
	if (u <= 0.0f)
		return 0.0f;
	if (u <= 0.0031308f)
		return u * 12.92f;
	return 1.055f * std::pow(u, 1.0f / 2.4f) - 0.055f;
}

inline float LumaLinear(float r, float g, float b)
{
	return r * 0.2126f + g * 0.7152f + b * 0.0722f;
}

inline float LumaNonlinear(float r, float g, float b)
{
	return r * 0.2627f + g * 0.6780f + b * 0.0593f;
}

inline float SaturationFromRgb(float r, float g, float b)
{
	const float max_c = std::max(r, std::max(g, b));
	const float min_c = std::min(r, std::min(g, b));
	if (max_c <= kEpsilon)
		return 0.0f;
	return Clamp01((max_c - min_c) / max_c);
}

inline Oklab LinearSrgbToOklab(const Rgb &lin)
{
	const float l = 0.4122214708f * lin.r + 0.5363325363f * lin.g + 0.0514459929f * lin.b;
	const float m = 0.2119034982f * lin.r + 0.6806995451f * lin.g + 0.1073969566f * lin.b;
	const float s = 0.0883024619f * lin.r + 0.2817188376f * lin.g + 0.6299787005f * lin.b;

	const float l_root = std::cbrt(std::max(l, 0.0f));
	const float m_root = std::cbrt(std::max(m, 0.0f));
	const float s_root = std::cbrt(std::max(s, 0.0f));

	Oklab out{};
	out.L = 0.2104542553f * l_root + 0.7936177850f * m_root - 0.0040720468f * s_root;
	out.a = 1.9779984951f * l_root - 2.4285922050f * m_root + 0.4505937099f * s_root;
	out.b = 0.0259040371f * l_root + 0.7827717662f * m_root - 0.8086757660f * s_root;
	return out;
}

inline Rgb OklabToLinearSrgb(const Oklab &lab)
{
	const float l_root = lab.L + 0.3963377774f * lab.a + 0.2158037573f * lab.b;
	const float m_root = lab.L - 0.1055613458f * lab.a - 0.0638541728f * lab.b;
	const float s_root = lab.L - 0.0894841775f * lab.a - 1.2914855480f * lab.b;

	const float l = l_root * l_root * l_root;
	const float m = m_root * m_root * m_root;
	const float s = s_root * s_root * s_root;

	Rgb out{};
	out.r = 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
	out.g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
	out.b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
	return out;
}

} // namespace lenses_autograde::deterministic
