#include "lenses/io/lenses-model-catalog.h"

#include <obs-data.h>
#include <obs-module.h>
#include <util/bmem.h>
#include <util/dstr.h>
#include <util/platform.h>

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <stdlib.h>

static bool lenses_has_onnx_extension(const char *path)
{
	if (!path || !*path)
		return false;

	const char *ext = os_get_path_extension(path);
	if (!ext || !*ext)
		return false;
	if (*ext == '.')
		ext++;

#ifdef _WIN32
	return _stricmp(ext, "onnx") == 0;
#else
	return strcasecmp(ext, "onnx") == 0;
#endif
}

static const char *lenses_safe_string(const char *value, const char *fallback)
{
	return (value && *value) ? value : fallback;
}

static char lenses_normalize_size_tier(const char *size_tier)
{
	if (!size_tier || !*size_tier)
		return '\0';

	const char c = (char)tolower((unsigned char)size_tier[0]);
	switch (c) {
	case 'n':
	case 's':
	case 'm':
	case 'l':
	case 'x':
		return c;
	default:
		return '\0';
	}
}

static char lenses_infer_size_tier_from_text(const char *text)
{
	if (!text || !*text)
		return '\0';

	const size_t len = strlen(text);
	for (size_t i = 0; i < len; ++i) {
		const char c = (char)tolower((unsigned char)text[i]);
		if (c != 'n' && c != 's' && c != 'm' && c != 'l' && c != 'x')
			continue;

		const char prev = i == 0 ? '\0' : (char)tolower((unsigned char)text[i - 1]);
		const char next = (i + 1U) < len ? (char)tolower((unsigned char)text[i + 1U]) : '\0';
		const bool prev_is_alnum = (prev >= 'a' && prev <= 'z') || (prev >= '0' && prev <= '9');
		const bool next_is_alnum = (next >= 'a' && next <= 'z') || (next >= '0' && next <= '9');
		const bool prev_is_digit = (prev >= '0' && prev <= '9');
		if (next_is_alnum)
			continue;
		if (prev_is_alnum && !prev_is_digit)
			continue;

		return c;
	}

	return '\0';
}

static void lenses_assign_size_tier(struct lenses_model_catalog_entry *entry, const char *explicit_size)
{
	if (!entry)
		return;

	entry->size_tier[0] = '\0';
	char tier = lenses_normalize_size_tier(explicit_size);
	if (!tier)
		tier = lenses_infer_size_tier_from_text(entry->id);
	if (!tier)
		tier = lenses_infer_size_tier_from_text(entry->name);
	if (!tier)
		tier = lenses_infer_size_tier_from_text(entry->model_path);
	if (!tier)
		return;

	entry->size_tier[0] = tier;
	entry->size_tier[1] = '\0';
}

static void lenses_write_status(char *status, size_t status_size, const char *text)
{
	if (!status || status_size == 0)
		return;

	snprintf(status, status_size, "%s", lenses_safe_string(text, ""));
}

static char *lenses_resolve_builtin_models_root(void)
{
	char *models_path = obs_module_file("models");
	if (models_path)
		return models_path;

	obs_module_t *module = obs_current_module();
	const char *data_path = module ? obs_get_module_data_path(module) : NULL;
	if (!data_path || !*data_path)
		return NULL;

	struct dstr fallback = {0};
	dstr_printf(&fallback, "%s/models", data_path);
	char *resolved = NULL;
	if (fallback.array && os_file_exists(fallback.array))
		resolved = bstrdup(fallback.array);
	dstr_free(&fallback);
	return resolved;
}

static int lenses_find_index_by_id(const struct lenses_model_catalog *catalog, const char *id)
{
	if (!catalog || !id || !*id)
		return -1;

	for (size_t i = 0; i < catalog->count; ++i) {
		if (strcmp(catalog->entries[i].id, id) == 0)
			return (int)i;
	}
	return -1;
}

static bool lenses_register_entry(struct lenses_model_catalog *catalog,
				  const struct lenses_model_catalog_entry *entry)
{
	if (!catalog || !entry || !entry->id[0] || !entry->model_path[0])
		return false;

	const int existing = lenses_find_index_by_id(catalog, entry->id);
	if (existing >= 0) {
		struct lenses_model_catalog_entry *slot = &catalog->entries[(size_t)existing];
		/* User-supplied model packages can override bundled ids. */
		if (slot->built_in && !entry->built_in)
			*slot = *entry;
		return true;
	}

	if (catalog->count >= LENSES_MODEL_CATALOG_MAX_ENTRIES)
		return false;

	catalog->entries[catalog->count++] = *entry;
	return true;
}

static bool lenses_extract_manifest_string(obs_data_t *root, const char *key, char *dest, size_t dest_size)
{
	const char *value = obs_data_get_string(root, key);
	if (value && *value) {
		snprintf(dest, dest_size, "%s", value);
		return true;
	}
	return false;
}

static bool lenses_parse_positive_u32_from_data(obs_data_t *obj, const char *key, uint32_t *out_value)
{
	if (!obj || !key || !*key || !out_value)
		return false;

	const int64_t value = obs_data_get_int(obj, key);
	if (value <= 0 || value > UINT32_MAX)
		return false;

	*out_value = (uint32_t)value;
	return true;
}

static bool lenses_extract_model_imgsz(obs_data_t *model, uint32_t *out_width, uint32_t *out_height)
{
	if (!model || !out_width || !out_height)
		return false;

	uint32_t scalar = 0;
	if (lenses_parse_positive_u32_from_data(model, "imgsz", &scalar)) {
		*out_width = scalar;
		*out_height = scalar;
		return true;
	}

	obs_data_array_t *imgsz = obs_data_get_array(model, "imgsz");
	if (!imgsz)
		return false;

	const size_t count = obs_data_array_count(imgsz);
	if (count == 0) {
		obs_data_array_release(imgsz);
		return false;
	}

	uint32_t values[2] = {0};
	size_t parsed = 0;
	const size_t limit = count < 2 ? count : 2;
	for (size_t i = 0; i < limit; ++i) {
		obs_data_t *item = obs_data_array_item(imgsz, i);
		if (!item)
			continue;

		uint32_t value = 0;
		if (lenses_parse_positive_u32_from_data(item, "value", &value))
			values[parsed++] = value;
		obs_data_release(item);
	}

	obs_data_array_release(imgsz);
	if (parsed == 0)
		return false;

	if (parsed == 1) {
		*out_width = values[0];
		*out_height = values[0];
		return true;
	}

	*out_width = values[0];
	*out_height = values[1];
	return true;
}

static bool lenses_extract_model_bool(obs_data_t *model, const char *key, bool default_value)
{
	if (!model || !key || !*key)
		return default_value;
	if (!obs_data_has_user_value(model, key))
		return default_value;
	return obs_data_get_bool(model, key);
}

static bool lenses_build_model_path(const char *package_path, const char *relative_file, char *out_path,
				    size_t out_size)
{
	if (!package_path || !*package_path || !relative_file || !*relative_file || !out_path || out_size == 0)
		return false;

	struct dstr path = {0};
	dstr_printf(&path, "%s/%s", package_path, relative_file);
	snprintf(out_path, out_size, "%s", path.array ? path.array : "");
	dstr_free(&path);
	return out_path[0] != '\0';
}

static bool lenses_collect_manifest_package(struct lenses_model_catalog *catalog, const char *package_path,
					    bool built_in)
{
	struct dstr metadata_path = {0};
	dstr_printf(&metadata_path, "%s/metadata.json", package_path);
	if (!metadata_path.array || !os_file_exists(metadata_path.array)) {
		dstr_free(&metadata_path);
		return false;
	}

	obs_data_t *root = obs_data_create_from_json_file(metadata_path.array);
	dstr_free(&metadata_path);
	if (!root)
		return false;

	struct lenses_model_catalog_entry entry = {0};
	entry.built_in = built_in;
	snprintf(entry.package_path, sizeof(entry.package_path), "%s", package_path);

	obs_data_t *metadata = obs_data_get_obj(root, "metadata");
	const char *directory_name = strrchr(package_path, '/');
	if (!directory_name)
		directory_name = package_path;
	else
		directory_name++;

	if (!lenses_extract_manifest_string(root, "id", entry.id, sizeof(entry.id)) && metadata) {
		(void)lenses_extract_manifest_string(metadata, "id", entry.id, sizeof(entry.id));
	}
	if (!entry.id[0])
		snprintf(entry.id, sizeof(entry.id), "%s", directory_name);

	if (!lenses_extract_manifest_string(root, "name", entry.name, sizeof(entry.name)) && metadata) {
		(void)lenses_extract_manifest_string(metadata, "name", entry.name, sizeof(entry.name));
	}
	if (!entry.name[0])
		snprintf(entry.name, sizeof(entry.name), "%s", entry.id);

	char size_tier[16] = {0};
	if (!lenses_extract_manifest_string(root, "size_tier", size_tier, sizeof(size_tier)) && metadata)
		(void)lenses_extract_manifest_string(metadata, "size_tier", size_tier, sizeof(size_tier));

	entry.class_count = (uint32_t)obs_data_get_int(root, "class_count");
	entry.input_width = 0;
	entry.input_height = 0;
	entry.dynamic_shape = true;
	entry.static_input = false;
	entry.static_output = false;
	entry.supports_iobinding_static_outputs = false;

	char model_file[256] = {0};
	obs_data_t *model = obs_data_get_obj(root, "model");
	if (model) {
		(void)lenses_extract_manifest_string(model, "file", model_file, sizeof(model_file));
		(void)lenses_extract_model_imgsz(model, &entry.input_width, &entry.input_height);
		entry.dynamic_shape = lenses_extract_model_bool(model, "dynamic", true);
		entry.static_input =
			lenses_extract_model_bool(model, "static_input", !entry.dynamic_shape);
		entry.static_output = lenses_extract_model_bool(model, "static_output", false);
		entry.supports_iobinding_static_outputs =
			lenses_extract_model_bool(model, "supports_iobinding_static_outputs",
					      entry.static_output);
		obs_data_release(model);
	}
	if (!model_file[0]) {
		(void)lenses_extract_manifest_string(root, "model_file", model_file, sizeof(model_file));
	}
	if (!model_file[0]) {
		snprintf(model_file, sizeof(model_file), "model.onnx");
	}

	const bool model_path_ok = lenses_build_model_path(package_path, model_file, entry.model_path,
							   sizeof(entry.model_path));
	if (!model_path_ok || !os_file_exists(entry.model_path) || !lenses_has_onnx_extension(entry.model_path)) {
		if (metadata)
			obs_data_release(metadata);
		obs_data_release(root);
		return false;
	}

	if (metadata)
		obs_data_release(metadata);
	obs_data_release(root);
	lenses_assign_size_tier(&entry, size_tier);
	return lenses_register_entry(catalog, &entry);
}

static bool lenses_collect_loose_onnx(struct lenses_model_catalog *catalog, const char *root_path,
				      const struct os_dirent *dirent, bool built_in)
{
	if (!catalog || !root_path || !dirent || !dirent->d_name[0] || dirent->directory)
		return false;
	if (!lenses_has_onnx_extension(dirent->d_name))
		return false;

	struct lenses_model_catalog_entry entry = {0};
	entry.built_in = built_in;
	snprintf(entry.id, sizeof(entry.id), "onnx:%s", dirent->d_name);
	snprintf(entry.name, sizeof(entry.name), "%s", dirent->d_name);
	snprintf(entry.package_path, sizeof(entry.package_path), "%s", root_path);

	struct dstr model_path = {0};
	dstr_printf(&model_path, "%s/%s", root_path, dirent->d_name);
	snprintf(entry.model_path, sizeof(entry.model_path), "%s", model_path.array ? model_path.array : "");
	dstr_free(&model_path);
	if (!entry.model_path[0])
		return false;
	if (!os_file_exists(entry.model_path))
		return false;
	lenses_assign_size_tier(&entry, NULL);

	return lenses_register_entry(catalog, &entry);
}

static void lenses_collect_from_root(struct lenses_model_catalog *catalog, const char *root_path, bool built_in)
{
	if (!catalog || !root_path || !*root_path)
		return;
	if (!os_file_exists(root_path))
		return;

	os_dir_t *dir = os_opendir(root_path);
	if (!dir)
		return;

	struct os_dirent *entry = NULL;
	while ((entry = os_readdir(dir)) != NULL) {
		if (!entry->d_name[0] || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;

		if (entry->directory) {
			struct dstr package_path = {0};
			dstr_printf(&package_path, "%s/%s", root_path, entry->d_name);
			if (package_path.array)
				(void)lenses_collect_manifest_package(catalog, package_path.array, built_in);
			dstr_free(&package_path);
			continue;
		}

		(void)lenses_collect_loose_onnx(catalog, root_path, entry, built_in);
	}

	os_closedir(dir);
}

static int lenses_model_entry_compare(const void *lhs_ptr, const void *rhs_ptr)
{
	const struct lenses_model_catalog_entry *lhs = lhs_ptr;
	const struct lenses_model_catalog_entry *rhs = rhs_ptr;

	if (lhs->built_in != rhs->built_in)
		return lhs->built_in ? -1 : 1;

	const char lhs_tier = lenses_normalize_size_tier(lhs->size_tier);
	const char rhs_tier = lenses_normalize_size_tier(rhs->size_tier);
	if (lhs_tier != rhs_tier) {
		const char order[] = {'n', 's', 'm', 'l', 'x'};
		int lhs_rank = 99;
		int rhs_rank = 99;
		for (size_t i = 0; i < sizeof(order); ++i) {
			if (lhs_tier == order[i])
				lhs_rank = (int)i;
			if (rhs_tier == order[i])
				rhs_rank = (int)i;
		}
		if (lhs_rank != rhs_rank)
			return lhs_rank - rhs_rank;
	}

	const int name_cmp = strcmp(lhs->name, rhs->name);
	if (name_cmp != 0)
		return name_cmp;

	return strcmp(lhs->id, rhs->id);
}

void lenses_model_catalog_clear(struct lenses_model_catalog *catalog)
{
	if (!catalog)
		return;
	memset(catalog, 0, sizeof(*catalog));
}

bool lenses_model_catalog_reload(struct lenses_model_catalog *catalog, char *status, size_t status_size)
{
	if (!catalog) {
		lenses_write_status(status, status_size, "Model catalog unavailable");
		return false;
	}

	lenses_model_catalog_clear(catalog);

	char *builtin_models_path = lenses_resolve_builtin_models_root();
	if (builtin_models_path) {
		lenses_collect_from_root(catalog, builtin_models_path, true);
		bfree(builtin_models_path);
	} else {
		blog(LOG_WARNING,
		     "[lenses] bundled model catalog root is unavailable "
		     "(obs_module_file(\"models\") and data_path fallback both unavailable)");
	}

	char *user_models_path = obs_module_config_path("models");
	if (user_models_path) {
		(void)os_mkdirs(user_models_path);
		lenses_collect_from_root(catalog, user_models_path, false);
		bfree(user_models_path);
	}

	if (catalog->count == 0) {
		lenses_write_status(status, status_size,
				    "No model packages found. Add packages under plugin data/models or module config models.");
		return false;
	}

	qsort(catalog->entries, catalog->count, sizeof(catalog->entries[0]), lenses_model_entry_compare);

	bool has_n = false;
	bool has_s = false;
	bool has_m = false;
	bool has_l = false;
	bool has_x = false;
	for (size_t i = 0; i < catalog->count; ++i) {
		switch (lenses_normalize_size_tier(catalog->entries[i].size_tier)) {
		case 'n':
			has_n = true;
			break;
		case 's':
			has_s = true;
			break;
		case 'm':
			has_m = true;
			break;
		case 'l':
			has_l = true;
			break;
		case 'x':
			has_x = true;
			break;
		default:
			break;
		}
	}

	char sizes[64] = {0};
	snprintf(sizes, sizeof(sizes), "%s%s%s%s%s", has_n ? "n " : "", has_s ? "s " : "",
		 has_m ? "m " : "", has_l ? "l " : "", has_x ? "x " : "");
	if (!sizes[0])
		snprintf(sizes, sizeof(sizes), "none");
	else {
		const size_t len = strlen(sizes);
		if (len > 0 && sizes[len - 1] == ' ')
			sizes[len - 1] = '\0';
	}

	char buffer[LENSES_MODEL_STATUS_MAX] = {0};
	snprintf(buffer, sizeof(buffer), "Discovered %zu model package(s) | available quality tiers: %s",
		 catalog->count, sizes);
	lenses_write_status(status, status_size, buffer);
	return true;
}

const struct lenses_model_catalog_entry *lenses_model_catalog_find_by_id(const struct lenses_model_catalog *catalog,
									 const char *id)
{
	if (!catalog || !id || !*id)
		return NULL;

	for (size_t i = 0; i < catalog->count; ++i) {
		if (strcmp(catalog->entries[i].id, id) == 0)
			return &catalog->entries[i];
	}
	return NULL;
}

const struct lenses_model_catalog_entry *
lenses_model_catalog_find_by_path(const struct lenses_model_catalog *catalog, const char *path)
{
	if (!catalog || !path || !*path)
		return NULL;

	for (size_t i = 0; i < catalog->count; ++i) {
		if (strcmp(catalog->entries[i].model_path, path) == 0)
			return &catalog->entries[i];
	}
	return NULL;
}

const struct lenses_model_catalog_entry *
lenses_model_catalog_pick_default(const struct lenses_model_catalog *catalog)
{
	if (!catalog || catalog->count == 0)
		return NULL;

	/* Prefer bundled defaults first for turnkey UX. */
	for (size_t i = 0; i < catalog->count; ++i) {
		if (catalog->entries[i].built_in)
			return &catalog->entries[i];
	}
	return &catalog->entries[0];
}
