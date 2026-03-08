#!/bin/sh
set -eu

FRAMEWORKS_DIR="${1:-}"
PLUGIN_BIN="${2:-}"
shift 2 || true

if [ -z "$FRAMEWORKS_DIR" ] || [ -z "$PLUGIN_BIN" ]; then
	exit 1
fi

if [ ! -d "$FRAMEWORKS_DIR" ] || [ ! -f "$PLUGIN_BIN" ]; then
	exit 0
fi

copy_and_prepare_lib() {
	src="$1"
	if [ ! -f "$src" ]; then
		return 0
	fi

	base="$(basename "$src")"
	dst="${FRAMEWORKS_DIR}/${base}"
	if [ ! -f "$dst" ]; then
		cp -f "$src" "$dst"
	fi

	soname_line="$(otool -D "$dst" 2>/dev/null | tail -n 1 || true)"
	soname_base="${soname_line##*/}"
	if [ -z "$soname_base" ]; then
		soname_base="$base"
	fi

	if [ "$soname_base" != "$base" ] && [ ! -f "${FRAMEWORKS_DIR}/${soname_base}" ]; then
		cp -f "$dst" "${FRAMEWORKS_DIR}/${soname_base}"
	fi

	/usr/bin/install_name_tool -id "@rpath/${soname_base}" "$dst" >/dev/null 2>&1 || true
	if [ "$soname_base" != "$base" ]; then
		/usr/bin/install_name_tool -id "@rpath/${soname_base}" \
			"${FRAMEWORKS_DIR}/${soname_base}" >/dev/null 2>&1 || true
	fi
}

patch_deps() {
	target="$1"
	if [ ! -f "$target" ]; then
		return 0
	fi

	otool -L "$target" 2>/dev/null | awk 'NR > 1 {print $1}' | while IFS= read -r dep_path; do
		dep_base="${dep_path##*/}"
		if [ -f "${FRAMEWORKS_DIR}/${dep_base}" ]; then
			/usr/bin/install_name_tool -change "$dep_path" "@rpath/${dep_base}" "$target" \
				>/dev/null 2>&1 || true
		fi
	done
}

for lib_path in "$@"; do
	copy_and_prepare_lib "$lib_path"
done

patch_deps "$PLUGIN_BIN"
for dylib in "${FRAMEWORKS_DIR}"/libopencv*.dylib; do
	if [ -f "$dylib" ]; then
		patch_deps "$dylib"
	fi
done

if command -v /usr/bin/codesign >/dev/null 2>&1; then
	for dylib in "${FRAMEWORKS_DIR}"/libopencv*.dylib; do
		if [ -f "$dylib" ]; then
			/usr/bin/codesign --force --sign - --timestamp=none "$dylib" >/dev/null 2>&1 || true
		fi
	done
	/usr/bin/codesign --force --sign - --timestamp=none "$PLUGIN_BIN" >/dev/null 2>&1 || true
fi
