#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LENSES_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEPS_ROOT="${LENSES_ORT_DEPS_ROOT:-${LENSES_ROOT}/.deps_vendor}"
ORT_VERSION="${ORT_VERSION:-v1.19.2}"
ORT_CONFIG="${ORT_CONFIG:-Release}"
ORT_DEPLOY_TARGET="${ORT_DEPLOY_TARGET:-13.3}"
# Supported values:
#   arm64
#   x86_64
#   universal2
ORT_ARCH="${ORT_ARCH:-arm64}"
ORT_USE_PREINSTALLED_EIGEN="${ORT_USE_PREINSTALLED_EIGEN:-auto}" # auto|on|off
ORT_EIGEN_PATH="${ORT_EIGEN_PATH:-}"
ORT_USE_FULL_PROTOBUF="${ORT_USE_FULL_PROTOBUF:-on}" # on|off

ORT_SRC_DIR="${DEPS_ROOT}/onnxruntime-src-${ORT_VERSION}"
ORT_BUILD_DIR="${DEPS_ROOT}/onnxruntime-build-${ORT_VERSION}"
ORT_OUTPUT_ROOT="${DEPS_ROOT}/onnxruntime"

log() {
  printf '[lenses-ort] %s\n' "$*"
}

ensure_tools() {
  command -v python3 >/dev/null 2>&1 || {
    echo "python3 is required" >&2
    exit 1
  }
  command -v git >/dev/null 2>&1 || {
    echo "git is required" >&2
    exit 1
  }
}

clone_onnxruntime() {
  mkdir -p "${DEPS_ROOT}"
  if [[ ! -d "${ORT_SRC_DIR}" ]]; then
    log "Cloning ONNX Runtime ${ORT_VERSION} ..."
    git clone --branch "${ORT_VERSION}" --depth 1 \
      https://github.com/microsoft/onnxruntime.git "${ORT_SRC_DIR}"
    (
      cd "${ORT_SRC_DIR}"
      git submodule update --init --recursive --depth 1
    )
  fi
}

normalize_eigen_path() {
  local candidate="$1"
  if [[ -z "${candidate}" ]]; then
    return 1
  fi

  if [[ -f "${candidate}/Eigen/Core" ]]; then
    printf '%s\n' "${candidate}"
    return 0
  fi
  if [[ -f "${candidate}/include/eigen3/Eigen/Core" ]]; then
    printf '%s\n' "${candidate}/include/eigen3"
    return 0
  fi
  if [[ -f "${candidate}/include/Eigen/Core" ]]; then
    printf '%s\n' "${candidate}/include"
    return 0
  fi
  return 1
}

resolve_eigen_path() {
  local path=""
  if path="$(normalize_eigen_path "${ORT_EIGEN_PATH}")"; then
    printf '%s\n' "${path}"
    return 0
  fi

  if command -v brew >/dev/null 2>&1; then
    local brew_eigen_prefix=""
    brew_eigen_prefix="$(brew --prefix eigen 2>/dev/null || true)"
    if path="$(normalize_eigen_path "${brew_eigen_prefix}")"; then
      printf '%s\n' "${path}"
      return 0
    fi
  fi

  if path="$(normalize_eigen_path "/opt/homebrew/include/eigen3")"; then
    printf '%s\n' "${path}"
    return 0
  fi
  if path="$(normalize_eigen_path "/usr/local/include/eigen3")"; then
    printf '%s\n' "${path}"
    return 0
  fi

  return 1
}

ort_supports_preinstalled_eigen() {
  local eigen_cmake="${ORT_SRC_DIR}/cmake/external/eigen.cmake"
  if [[ ! -f "${eigen_cmake}" ]]; then
    return 1
  fi

  grep -q "onnxruntime_USE_PREINSTALLED_EIGEN" "${eigen_cmake}"
}

build_arch() {
  local arch="$1"
  local arch_build_dir="${ORT_BUILD_DIR}/${arch}"
  local build_py="${ORT_SRC_DIR}/tools/ci_build/build.py"
  local eigen_path=""
  local -a cmake_extra_defines=(
    "CMAKE_OSX_DEPLOYMENT_TARGET=${ORT_DEPLOY_TARGET}"
    "CMAKE_POLICY_VERSION_MINIMUM=3.5"
  )

  mkdir -p "${arch_build_dir}"
  log "Building ORT (${arch}, ${ORT_CONFIG}) with CoreML EP ..."

  case "${ORT_USE_PREINSTALLED_EIGEN}" in
    auto|on)
      if eigen_path="$(resolve_eigen_path)"; then
        if ort_supports_preinstalled_eigen; then
          cmake_extra_defines+=(
            "onnxruntime_USE_PREINSTALLED_EIGEN=ON"
            "eigen_SOURCE_PATH=${eigen_path}"
          )
          log "Using preinstalled Eigen headers: ${eigen_path}"
        else
          log "ORT ${ORT_VERSION} does not expose preinstalled Eigen hook; using pinned Eigen fetch."
        fi
      elif [[ "${ORT_USE_PREINSTALLED_EIGEN}" == "on" ]]; then
        echo "ORT_USE_PREINSTALLED_EIGEN=on but Eigen headers were not found" >&2
        echo "Set ORT_EIGEN_PATH or install Eigen (e.g. 'brew install eigen')." >&2
        exit 1
      fi
      ;;
    off)
      ;;
    *)
      echo "Unsupported ORT_USE_PREINSTALLED_EIGEN '${ORT_USE_PREINSTALLED_EIGEN}' (expected auto|on|off)" >&2
      exit 1
      ;;
  esac

  local -a protobuf_args=()
  case "${ORT_USE_FULL_PROTOBUF}" in
    on)
      protobuf_args=(--use_full_protobuf)
      ;;
    off)
      ;;
    *)
      echo "Unsupported ORT_USE_FULL_PROTOBUF '${ORT_USE_FULL_PROTOBUF}' (expected on|off)" >&2
      exit 1
      ;;
  esac

  python3 "${build_py}" \
    --build_dir "${arch_build_dir}" \
    --config "${ORT_CONFIG}" \
    --update \
    --build \
    --parallel \
    --skip_submodule_sync \
    --skip_tests \
    --build_shared_lib \
    --compile_no_warning_as_error \
    --use_coreml \
    --apple_deploy_target "${ORT_DEPLOY_TARGET}" \
    --osx_arch "${arch}" \
    "${protobuf_args[@]}" \
    --cmake_extra_defines \
      "${cmake_extra_defines[@]}"
}

find_main_dylib() {
  local search_root="$1"
  local candidate=""

  candidate="$(find "${search_root}" -type f -name 'libonnxruntime.[0-9]*.dylib' | head -n 1 || true)"
  if [[ -z "${candidate}" ]]; then
    candidate="$(find "${search_root}" -type f -name 'libonnxruntime.dylib' | head -n 1 || true)"
  fi
  if [[ -z "${candidate}" ]]; then
    echo "Could not locate libonnxruntime dylib under ${search_root}" >&2
    exit 1
  fi

  printf '%s\n' "${candidate}"
}

publish_output() {
  mkdir -p "${ORT_OUTPUT_ROOT}/include" "${ORT_OUTPUT_ROOT}/lib"

  local arm_dylib=""
  local x86_dylib=""

  if [[ "${ORT_ARCH}" == "arm64" || "${ORT_ARCH}" == "universal2" ]]; then
    arm_dylib="$(find_main_dylib "${ORT_BUILD_DIR}/arm64")"
  fi
  if [[ "${ORT_ARCH}" == "x86_64" || "${ORT_ARCH}" == "universal2" ]]; then
    x86_dylib="$(find_main_dylib "${ORT_BUILD_DIR}/x86_64")"
  fi

  if [[ "${ORT_ARCH}" == "universal2" ]]; then
    log "Creating universal2 libonnxruntime.dylib ..."
    lipo -create "${arm_dylib}" "${x86_dylib}" \
      -output "${ORT_OUTPUT_ROOT}/lib/libonnxruntime.dylib"
  elif [[ "${ORT_ARCH}" == "arm64" ]]; then
    cp -f "${arm_dylib}" "${ORT_OUTPUT_ROOT}/lib/libonnxruntime.dylib"
  elif [[ "${ORT_ARCH}" == "x86_64" ]]; then
    cp -f "${x86_dylib}" "${ORT_OUTPUT_ROOT}/lib/libonnxruntime.dylib"
  else
    echo "Unsupported ORT_ARCH '${ORT_ARCH}' (expected arm64|x86_64|universal2)" >&2
    exit 1
  fi

  # Keep SONAME stable for @rpath consumers and CMake copy steps.
  install_name_tool -id "@rpath/libonnxruntime.dylib" \
    "${ORT_OUTPUT_ROOT}/lib/libonnxruntime.dylib" || true

  rm -rf "${ORT_OUTPUT_ROOT}/include/onnxruntime"
  cp -a "${ORT_SRC_DIR}/include/onnxruntime" "${ORT_OUTPUT_ROOT}/include/"
  # Preserve top-level session headers expected by consumers that include
  # <onnxruntime_*.h> directly.
  local session_headers_dir="${ORT_SRC_DIR}/include/onnxruntime/core/session"
  if [[ -d "${session_headers_dir}" ]]; then
    find "${session_headers_dir}" -maxdepth 1 -type f -name 'onnxruntime*.h' -exec cp -f {} "${ORT_OUTPUT_ROOT}/include/" \;
  fi

  if [[ -f "${ORT_SRC_DIR}/LICENSE" ]]; then
    cp -f "${ORT_SRC_DIR}/LICENSE" "${ORT_OUTPUT_ROOT}/LICENSE"
  fi

  log "ORT bundle ready:"
  log "  root: ${ORT_OUTPUT_ROOT}"
  log "  include: ${ORT_OUTPUT_ROOT}/include/onnxruntime"
  log "  dylib: ${ORT_OUTPUT_ROOT}/lib/libonnxruntime.dylib"
}

main() {
  ensure_tools
  clone_onnxruntime

  case "${ORT_ARCH}" in
    arm64)
      build_arch arm64
      ;;
    x86_64)
      build_arch x86_64
      ;;
    universal2)
      build_arch arm64
      build_arch x86_64
      ;;
    *)
      echo "Unsupported ORT_ARCH '${ORT_ARCH}' (expected arm64|x86_64|universal2)" >&2
      exit 1
      ;;
  esac

  publish_output

  cat <<EOF

Next step:
  cmake -S . -B build_macos -DLENSES_ONNXRUNTIME_ROOT="${ORT_OUTPUT_ROOT}"
  cmake --build build_macos --target obs-studio -j12
EOF
}

main "$@"
