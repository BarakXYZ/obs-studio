#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LENSES_MODEL_CATALOG_MAX_ENTRIES 64
#define LENSES_MODEL_ID_MAX 128
#define LENSES_MODEL_NAME_MAX 192
#define LENSES_MODEL_PATH_MAX 1024
#define LENSES_MODEL_STATUS_MAX 256
#define LENSES_MODEL_SIZE_TIER_MAX 8

struct lenses_model_catalog_entry {
	char id[LENSES_MODEL_ID_MAX];
	char name[LENSES_MODEL_NAME_MAX];
	char model_path[LENSES_MODEL_PATH_MAX];
	char package_path[LENSES_MODEL_PATH_MAX];
	char size_tier[LENSES_MODEL_SIZE_TIER_MAX];
	uint32_t class_count;
	uint32_t input_width;
	uint32_t input_height;
	bool dynamic_shape;
	bool static_input;
	bool static_output;
	bool supports_iobinding_static_outputs;
	bool built_in;
};

struct lenses_model_catalog {
	struct lenses_model_catalog_entry entries[LENSES_MODEL_CATALOG_MAX_ENTRIES];
	size_t count;
};

void lenses_model_catalog_clear(struct lenses_model_catalog *catalog);
bool lenses_model_catalog_reload(struct lenses_model_catalog *catalog, char *status, size_t status_size);
const struct lenses_model_catalog_entry *lenses_model_catalog_find_by_id(const struct lenses_model_catalog *catalog,
									 const char *id);
const struct lenses_model_catalog_entry *
lenses_model_catalog_find_by_path(const struct lenses_model_catalog *catalog, const char *path);
const struct lenses_model_catalog_entry *
lenses_model_catalog_pick_default(const struct lenses_model_catalog *catalog);
const struct lenses_model_catalog_entry *
lenses_model_catalog_pick_by_size(const struct lenses_model_catalog *catalog, const char *size_tier,
				  bool prefer_built_in);

#ifdef __cplusplus
}
#endif
