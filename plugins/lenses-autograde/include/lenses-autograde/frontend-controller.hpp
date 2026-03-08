#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <obs.h>

namespace lenses_autograde::frontend {

void Initialize();
void Shutdown();

void RegisterFilterInstance(const std::string &uuid, obs_source_t *source);
void UnregisterFilterInstance(const std::string &uuid);

void SetActiveInstance(const std::string &uuid);
bool RequestSelectionForActive();

void SubmitSourceSnapshot(const std::string &uuid, uint32_t width, uint32_t height, uint32_t linesize,
			  const uint8_t *bgra, size_t bgra_size);

} // namespace lenses_autograde::frontend
