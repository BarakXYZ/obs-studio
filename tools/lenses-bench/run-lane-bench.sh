#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

BUILD_DIR="build_macos"
CONFIG="RelWithDebInfo"
LANE="all"
CLIPS_DIR="$ROOT_DIR/bench/media"
MODEL_PATH="$ROOT_DIR/plugins/lenses/data/models/yolo11s-seg-coco80/model.onnx"
PROVIDER="ort-coreml"
INPUT_WIDTH="640"
INPUT_HEIGHT="640"
TARGET_FPS="30"
SUBMIT_FPS_MODE="target"
WARMUP_SECONDS="2"
DRAIN_TIMEOUT_MS="8000"
WALL_CLOCK="1"
REPEAT="1"
REPEAT_FAST="0"
REPEAT_FULL="0"
EXPECTED_BACKEND=""
EXPECTED_PROVIDER=""
REQUIRE_HEALTH_READY="1"
REQUIRE_NO_CPU_FALLBACK="1"
REQUIRE_CPU_FALLBACK_DISABLED="1"
OUTPUT_DIR=""
SKIP_BUILD="0"
ALLOW_FALLBACK="0"
FAIL_ON_ERROR="0"
FAIL_ON_INVALID="0"
FAIL_ON_THRESHOLD="0"
THRESHOLD_MIN_VALID_RATE=""
THRESHOLD_MIN_COMPLETE_FPS=""
THRESHOLD_MIN_COREML_COVERAGE_PCT=""
THRESHOLD_MAX_INFER_P95_MS=""
THRESHOLD_MAX_QUEUE_P95_MS=""
THRESHOLD_MAX_E2E_P95_MS=""
MAC_BENCH_FRAMEWORK_DST=""

usage() {
  cat <<USAGE
Usage: tools/lenses-bench/run-lane-bench.sh [options]

Options:
  --build-dir <dir>                Build directory (default: build_macos)
  --config <cfg>                   Build config (default: RelWithDebInfo)
  --lane <fast|full|all>           fast=10s, full=30s+60s, all=10s+30s+60s (default: all)
  --clips-dir <dir>                Clips directory (default: bench/media)
  --model <path>                   ONNX model path
  --provider <name>                Runtime provider (default: ort-coreml)
  --input-width <px>               Runtime input width (default: 640)
  --input-height <px>              Runtime input height (default: 640)
  --target-fps <fps>               Target AI FPS for scheduler + pacing math (default: 30)
  --submit-fps-mode <mode>         clip|target|min (default: target)
  --warmup-seconds <sec>           Uncaptured warmup window before measured pass (default: 2)
  --drain-timeout-ms <ms>          Drain timeout per run (default: 8000)
  --repeat <n>                     Repeat each case N times when lane-specific repeat not set
  --repeat-fast <n>                Override repeats for fast-lane clips only
  --repeat-full <n>                Override repeats for full-lane clips only
  --expected-backend <name>        Validity gate on reported backend (for example: ort)
  --expected-provider <name>       Validity gate on requested provider (for example: ort-coreml)
  --no-require-health-ready        Disable readiness validity gate
  --no-require-no-cpu-fallback     Allow runs with cpu_fallback_detected=1
  --no-require-cpu-fallback-disabled  Allow runs where cpu fallback gate is not enforced
  --threshold-min-valid-rate <pct> Threshold gate on valid run ratio (0-100)
  --threshold-min-complete-fps <v> Threshold gate on mean complete_fps (overall valid runs)
  --threshold-min-coreml-coverage-pct <pct>
                                   Threshold gate on mean CoreML coverage percent (overall valid runs)
  --threshold-max-infer-p95-ms <v> Threshold gate on mean infer_p95_ms (overall valid runs)
  --threshold-max-queue-p95-ms <v> Threshold gate on mean queue_p95_ms (overall valid runs)
  --threshold-max-e2e-p95-ms <v>   Threshold gate on mean e2e_p95_ms (overall valid runs)
  --output-dir <dir>               Output dir (default: bench/results/lenses-bench/<timestamp>)
  --skip-build                     Skip building lenses-benchmark
  --no-wall-clock                  Disable wall-clock pacing
  --allow-fallback                 Pass through benchmark fallback flag (disables strict CPU-fallback validity gates)
  --fail-on-error                  Exit non-zero if any run exits non-zero or reports success=0
  --fail-on-invalid                Exit non-zero if any run fails validity gates
  --fail-on-threshold              Exit non-zero if configured threshold gates fail
  -h, --help                       Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --lane) LANE="$2"; shift 2 ;;
    --clips-dir) CLIPS_DIR="$2"; shift 2 ;;
    --model) MODEL_PATH="$2"; shift 2 ;;
    --provider) PROVIDER="$2"; shift 2 ;;
    --input-width) INPUT_WIDTH="$2"; shift 2 ;;
    --input-height) INPUT_HEIGHT="$2"; shift 2 ;;
    --target-fps) TARGET_FPS="$2"; shift 2 ;;
    --submit-fps-mode) SUBMIT_FPS_MODE="$2"; shift 2 ;;
    --warmup-seconds) WARMUP_SECONDS="$2"; shift 2 ;;
    --drain-timeout-ms) DRAIN_TIMEOUT_MS="$2"; shift 2 ;;
    --repeat) REPEAT="$2"; shift 2 ;;
    --repeat-fast) REPEAT_FAST="$2"; shift 2 ;;
    --repeat-full) REPEAT_FULL="$2"; shift 2 ;;
    --expected-backend) EXPECTED_BACKEND="$2"; shift 2 ;;
    --expected-provider) EXPECTED_PROVIDER="$2"; shift 2 ;;
    --no-require-health-ready) REQUIRE_HEALTH_READY="0"; shift ;;
    --no-require-no-cpu-fallback) REQUIRE_NO_CPU_FALLBACK="0"; shift ;;
    --no-require-cpu-fallback-disabled) REQUIRE_CPU_FALLBACK_DISABLED="0"; shift ;;
    --threshold-min-valid-rate) THRESHOLD_MIN_VALID_RATE="$2"; shift 2 ;;
    --threshold-min-complete-fps) THRESHOLD_MIN_COMPLETE_FPS="$2"; shift 2 ;;
    --threshold-min-coreml-coverage-pct) THRESHOLD_MIN_COREML_COVERAGE_PCT="$2"; shift 2 ;;
    --threshold-max-infer-p95-ms) THRESHOLD_MAX_INFER_P95_MS="$2"; shift 2 ;;
    --threshold-max-queue-p95-ms) THRESHOLD_MAX_QUEUE_P95_MS="$2"; shift 2 ;;
    --threshold-max-e2e-p95-ms) THRESHOLD_MAX_E2E_P95_MS="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD="1"; shift ;;
    --no-wall-clock) WALL_CLOCK="0"; shift ;;
    --allow-fallback) ALLOW_FALLBACK="1"; shift ;;
    --fail-on-error) FAIL_ON_ERROR="1"; shift ;;
    --fail-on-invalid) FAIL_ON_INVALID="1"; shift ;;
    --fail-on-threshold) FAIL_ON_THRESHOLD="1"; shift ;;
    -h|--help) usage; exit 0 ;;
    *)
      echo "Unknown argument: $1" >&2
      usage
      exit 1
      ;;
  esac
done

if [[ "$LANE" != "fast" && "$LANE" != "full" && "$LANE" != "all" ]]; then
  echo "Invalid --lane '$LANE' (expected fast|full|all)" >&2
  exit 1
fi
if [[ "$SUBMIT_FPS_MODE" != "clip" && "$SUBMIT_FPS_MODE" != "target" && "$SUBMIT_FPS_MODE" != "min" ]]; then
  echo "Invalid --submit-fps-mode '$SUBMIT_FPS_MODE' (expected clip|target|min)" >&2
  exit 1
fi

if [[ "$ALLOW_FALLBACK" == "1" ]]; then
  REQUIRE_NO_CPU_FALLBACK="0"
  REQUIRE_CPU_FALLBACK_DISABLED="0"
fi

benchmark_bin="$ROOT_DIR/$BUILD_DIR/plugins/lenses/$CONFIG/lenses-benchmark"
if [[ "$SKIP_BUILD" == "0" ]]; then
  echo "[lenses-lane-bench] Building lenses-benchmark"
  cmake --build "$ROOT_DIR/$BUILD_DIR" --config "$CONFIG" --target lenses-benchmark >/dev/null
fi
if [[ ! -x "$benchmark_bin" ]]; then
  echo "Missing benchmark binary: $benchmark_bin" >&2
  exit 1
fi
if [[ ! -f "$MODEL_PATH" ]]; then
  echo "Missing model file: $MODEL_PATH" >&2
  exit 1
fi
if ! command -v ffprobe >/dev/null 2>&1; then
  echo "Missing required dependency: ffprobe" >&2
  exit 1
fi
if ! command -v ffmpeg >/dev/null 2>&1; then
  echo "Missing required dependency: ffmpeg" >&2
  exit 1
fi

if [[ -z "$OUTPUT_DIR" ]]; then
  timestamp="$(date +%Y%m%d-%H%M%S)"
  OUTPUT_DIR="$ROOT_DIR/bench/results/lenses-bench/$timestamp"
fi
mkdir -p "$OUTPUT_DIR/runs"

mkdir -p "$ROOT_DIR/bench"
if ! grep -q '^/bench/results/$' "$ROOT_DIR/.git/info/exclude" 2>/dev/null; then
  echo '/bench/results/' >> "$ROOT_DIR/.git/info/exclude"
fi

if [[ "$(uname -s)" == "Darwin" ]]; then
  framework_src="$ROOT_DIR/$BUILD_DIR/libobs/$CONFIG/libobs.framework"
  framework_dst_dir="$ROOT_DIR/$BUILD_DIR/plugins/lenses/Frameworks"
  framework_dst="$framework_dst_dir/libobs.framework"
  if [[ ! -d "$framework_src" ]]; then
    echo "Missing libobs.framework for benchmark runtime: $framework_src" >&2
    exit 1
  fi
  mkdir -p "$framework_dst_dir"
  rm -rf "$framework_dst"
  cp -R "$framework_src" "$framework_dst"
  MAC_BENCH_FRAMEWORK_DST="$framework_dst"
fi

macos_prepare_benchmark_signing() {
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return 0
  fi
  if [[ -z "$MAC_BENCH_FRAMEWORK_DST" || ! -d "$MAC_BENCH_FRAMEWORK_DST" ]]; then
    echo "Missing benchmark framework destination: $MAC_BENCH_FRAMEWORK_DST" >&2
    return 1
  fi
  if [[ ! -x "$benchmark_bin" ]]; then
    echo "Missing benchmark binary for signing: $benchmark_bin" >&2
    return 1
  fi

  # Make repeated benchmark launches deterministic under macOS library validation.
  local framework_binary="$MAC_BENCH_FRAMEWORK_DST/Versions/A/libobs"
  if [[ ! -f "$framework_binary" ]]; then
    echo "Missing libobs framework binary for signing: $framework_binary" >&2
    return 1
  fi

  xattr -dr com.apple.quarantine "$MAC_BENCH_FRAMEWORK_DST" "$benchmark_bin" >/dev/null 2>&1 || true
  codesign --force --sign - --timestamp=none "$framework_binary"
  codesign --force --sign - --timestamp=none "$MAC_BENCH_FRAMEWORK_DST"
  codesign --force --sign - --timestamp=none "$benchmark_bin"
}

resolve_clips() {
  case "$LANE" in
    fast)
      printf '%s\n' "$CLIPS_DIR/bench_1080p60_10s.mp4"
      ;;
    full)
      printf '%s\n' "$CLIPS_DIR/bench_1080p60_30s.mp4" "$CLIPS_DIR/bench_1080p60_60s.mp4"
      ;;
    all)
      printf '%s\n' "$CLIPS_DIR/bench_1080p60_10s.mp4" "$CLIPS_DIR/bench_1080p60_30s.mp4" "$CLIPS_DIR/bench_1080p60_60s.mp4"
      ;;
  esac
}

fps_from_fraction() {
  local frac="$1"
  awk -v frac="$frac" 'BEGIN {
    n=split(frac,a,"/");
    if (n == 2 && a[2] + 0.0 > 0.0) printf("%.6f", (a[1] + 0.0) / (a[2] + 0.0));
    else printf("0.000000");
  }'
}

round_int() {
  local value="$1"
  awk -v v="$value" 'BEGIN { printf("%d", int(v + 0.5)); }'
}

frames_from_duration_and_fps() {
  local duration="$1"
  local fps="$2"
  awk -v d="$duration" -v f="$fps" 'BEGIN {
    frames = int((d * f) + 0.5);
    if (frames < 1) frames = 1;
    printf("%d", frames);
  }'
}

min_int() {
  local a="$1"
  local b="$2"
  if (( a < b )); then
    printf '%d' "$a"
  else
    printf '%d' "$b"
  fi
}

repeat_for_lane() {
  local lane_name="$1"
  if [[ "$lane_name" == "fast" && "$REPEAT_FAST" != "0" ]]; then
    printf '%s' "$REPEAT_FAST"
    return
  fi
  if [[ "$lane_name" == "full" && "$REPEAT_FULL" != "0" ]]; then
    printf '%s' "$REPEAT_FULL"
    return
  fi
  printf '%s' "$REPEAT"
}

parse_value() {
  local key="$1"
  local file="$2"
  awk -v k="$key" 'BEGIN{v=""} {
    for(i=1;i<=NF;i++){
      if($i ~ ("^" k "=")) { split($i,a,"="); v=a[2]; }
    }
  } END{print v}' "$file"
}

parse_quoted() {
  local expr="$1"
  local file="$2"
  sed -n "$expr" "$file" | tail -n 1
}

parse_stage_metric() {
  local stage_label="$1"
  local metric_name="$2"
  local file="$3"
  awk -v label="$stage_label" -v metric="$metric_name" '
    $1==label {
      for(i=2;i<=NF;i++) {
        if($i ~ ("^" metric "=")) {
          split($i,a,"=");
          value=a[2]
        }
      }
    }
    END {
      if (value == "") value = "0"
      print value
    }
  ' "$file"
}

append_reason() {
  local current="$1"
  local addition="$2"
  if [[ -z "$current" ]]; then
    printf '%s' "$addition"
  else
    printf '%s,%s' "$current" "$addition"
  fi
}

raw_tsv="$OUTPUT_DIR/summary.tsv"
printf 'lane\tclip\trepeat\tduration_s\tclip_fps\tsubmit_fps_mode\tsubmit_fps\twarmup_frames\tmeasured_frames\texit_code\tsuccess\tvalid\tvalid_reason\tsubmitted\trejected\tcompleted\twall_ms\tsubmit_fps_out\tcomplete_fps\tinfer_p50_ms\tinfer_p95_ms\tinfer_p99_ms\tqueue_p50_ms\tqueue_p95_ms\tqueue_p99_ms\te2e_p50_ms\te2e_p95_ms\te2e_p99_ms\thealth_ready\tbackend\trequested_provider\tactive_provider\terror\tlog_path\tcpu_fallback_detected\tcpu_fallback_disabled\tcoreml_coverage_known\tcoreml_supported_nodes\tcoreml_total_nodes\tcoreml_supported_partitions\tcoreml_coverage_ratio\n' > "$raw_tsv"

any_failures=0
any_invalid=0

while IFS= read -r clip_path; do
  [[ -z "$clip_path" ]] && continue
  if [[ ! -f "$clip_path" ]]; then
    echo "Missing clip: $clip_path" >&2
    exit 1
  fi

  clip_name="$(basename "$clip_path")"
  duration_s="$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$clip_path")"
  clip_fps_frac="$(ffprobe -v error -select_streams v:0 -show_entries stream=avg_frame_rate -of csv=p=0 "$clip_path")"
  source_width="$(ffprobe -v error -select_streams v:0 -show_entries stream=width -of csv=p=0 "$clip_path")"
  source_height="$(ffprobe -v error -select_streams v:0 -show_entries stream=height -of csv=p=0 "$clip_path")"
  if [[ -z "$source_width" || -z "$source_height" ]]; then
    echo "Failed to read clip dimensions: $clip_path" >&2
    exit 1
  fi

  clip_fps="$(fps_from_fraction "$clip_fps_frac")"
  clip_submit_fps="$(round_int "$clip_fps")"
  target_submit_fps="$(round_int "$TARGET_FPS")"

  case "$SUBMIT_FPS_MODE" in
    clip) submit_fps="$clip_submit_fps" ;;
    target) submit_fps="$target_submit_fps" ;;
    min) submit_fps="$(min_int "$clip_submit_fps" "$target_submit_fps")" ;;
  esac

  measured_frames="$(frames_from_duration_and_fps "$duration_s" "$submit_fps")"
  warmup_frames="$(frames_from_duration_and_fps "$WARMUP_SECONDS" "$submit_fps")"

  lane_name="full"
  if [[ "$clip_name" == *"10s.mp4" ]]; then
    lane_name="fast"
  fi

  repeat_count="$(repeat_for_lane "$lane_name")"

  for ((run_idx=1; run_idx<=repeat_count; run_idx++)); do
    case_id="${lane_name}-${clip_name%.mp4}-r${run_idx}"
    run_log="$OUTPUT_DIR/runs/${case_id}.log"

    cmd=(
      "$benchmark_bin"
      --model "$MODEL_PATH"
      --provider "$PROVIDER"
      --input-width "$INPUT_WIDTH"
      --input-height "$INPUT_HEIGHT"
      --source-width "$source_width"
      --source-height "$source_height"
      --clip "$clip_path"
      --frames "$measured_frames"
      --warmup-frames "$warmup_frames"
      --submit-fps "$submit_fps"
      --target-fps "$TARGET_FPS"
      --drain-timeout-ms "$DRAIN_TIMEOUT_MS"
    )

    if [[ "$WALL_CLOCK" == "1" ]]; then
      cmd+=(--wall-clock)
    fi
    if [[ "$ALLOW_FALLBACK" == "1" ]]; then
      cmd+=(--allow-fallback)
    fi

    echo "[lenses-lane-bench] Running $case_id measured_frames=$measured_frames warmup_frames=$warmup_frames submit_fps=$submit_fps"

    set +e
    if [[ "$(uname -s)" == "Darwin" ]]; then
      if ! macos_prepare_benchmark_signing > "$run_log" 2>&1; then
        echo "benchmark_signing_failed" >> "$run_log"
        exit_code=125
      else
        DYLD_LIBRARY_PATH="$ROOT_DIR/.deps/obs-deps-2025-08-23-universal/lib:$ROOT_DIR/plugins/lenses/.deps_vendor/onnxruntime/lib" \
          "${cmd[@]}" > "$run_log" 2>&1
        exit_code=$?
      fi
    else
      "${cmd[@]}" > "$run_log" 2>&1
      exit_code=$?
    fi
    set -e

    success="$(parse_value success "$run_log")"
    submitted="$(parse_value submitted "$run_log")"
    rejected="$(parse_value rejected "$run_log")"
    completed="$(parse_value completed "$run_log")"
    wall_ms="$(parse_value wall_ms "$run_log")"
    submit_fps_out="$(parse_value submit_fps "$run_log")"
    complete_fps_out="$(parse_value complete_fps "$run_log")"

    infer_p50_ms="$(parse_stage_metric stage_p50 infer "$run_log")"
    infer_p95_ms="$(parse_stage_metric stage_p95 infer "$run_log")"
    infer_p99_ms="$(parse_stage_metric stage_p99 infer "$run_log")"
    queue_p50_ms="$(parse_stage_metric stage_p50 queue "$run_log")"
    queue_p95_ms="$(parse_stage_metric stage_p95 queue "$run_log")"
    queue_p99_ms="$(parse_stage_metric stage_p99 queue "$run_log")"
    e2e_p50_ms="$(parse_stage_metric stage_p50 e2e "$run_log")"
    e2e_p95_ms="$(parse_stage_metric stage_p95 e2e "$run_log")"
    e2e_p99_ms="$(parse_stage_metric stage_p99 e2e "$run_log")"

    health_ready="$(parse_value ready "$run_log")"
    cpu_fallback_detected="$(parse_value cpu_fallback_detected "$run_log")"
    cpu_fallback_disabled="$(parse_value cpu_fallback_disabled "$run_log")"
    backend="$(parse_quoted "s/^health ready=[01] backend='\\([^']*\\)'.*/\\1/p" "$run_log")"
    requested_provider="$(parse_quoted "s/^health ready=[01] backend='[^']*' requested_provider='\\([^']*\\)'.*/\\1/p" "$run_log")"
    active_provider="$(parse_quoted "s/^info: \[lenses\] ORT session ready\..* active_provider='\\([^']*\\)'.*/\\1/p" "$run_log")"
    if [[ -z "$active_provider" ]]; then
      active_provider="$(parse_quoted "s/^info: \[lenses\] ORT session ready\..* provider='\\([^']*\\)'.*/\\1/p" "$run_log")"
    fi
    if [[ -z "$requested_provider" ]]; then
      requested_provider="$(parse_quoted "s/^info: \[lenses\] runtime backend selection provider='\\([^']*\\)'.*/\\1/p" "$run_log")"
    fi
    error_msg="$(parse_quoted "s/^success=[01] error='\\(.*\\)'/\\1/p" "$run_log")"
    coreml_coverage_known="$(parse_value coreml_coverage_known "$run_log")"
    coreml_supported_nodes="$(parse_value coreml_supported_nodes "$run_log")"
    coreml_total_nodes="$(parse_value coreml_total_nodes "$run_log")"
    coreml_supported_partitions="$(parse_value coreml_supported_partitions "$run_log")"
    coreml_coverage_ratio="$(parse_value coreml_coverage_ratio "$run_log")"

    success="${success:-0}"
    submitted="${submitted:-0}"
    rejected="${rejected:-0}"
    completed="${completed:-0}"
    wall_ms="${wall_ms:-0}"
    submit_fps_out="${submit_fps_out:-0}"
    complete_fps_out="${complete_fps_out:-0}"
    infer_p50_ms="${infer_p50_ms:-0}"
    infer_p95_ms="${infer_p95_ms:-0}"
    infer_p99_ms="${infer_p99_ms:-0}"
    queue_p50_ms="${queue_p50_ms:-0}"
    queue_p95_ms="${queue_p95_ms:-0}"
    queue_p99_ms="${queue_p99_ms:-0}"
    e2e_p50_ms="${e2e_p50_ms:-0}"
    e2e_p95_ms="${e2e_p95_ms:-0}"
    e2e_p99_ms="${e2e_p99_ms:-0}"
    health_ready="${health_ready:-0}"
    cpu_fallback_detected="${cpu_fallback_detected:-0}"
    cpu_fallback_disabled="${cpu_fallback_disabled:-0}"
    coreml_coverage_known="${coreml_coverage_known:-0}"
    coreml_supported_nodes="${coreml_supported_nodes:-0}"
    coreml_total_nodes="${coreml_total_nodes:-0}"
    coreml_supported_partitions="${coreml_supported_partitions:-0}"
    coreml_coverage_ratio="${coreml_coverage_ratio:-0}"
    backend="${backend:-unknown}"
    requested_provider="${requested_provider:-unknown}"
    active_provider="${active_provider:-unknown}"
    error_msg="${error_msg:-}"

    valid="1"
    valid_reason=""

    if [[ "$exit_code" -ne 0 || "$success" != "1" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "run_failed")"
      any_failures=1
    fi
    if [[ "$REQUIRE_HEALTH_READY" == "1" && "$health_ready" != "1" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "health_not_ready")"
    fi
    if [[ -n "$EXPECTED_BACKEND" && "$backend" != "$EXPECTED_BACKEND" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "backend_mismatch")"
    fi
    if [[ -n "$EXPECTED_PROVIDER" && "$requested_provider" != "$EXPECTED_PROVIDER" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "provider_mismatch")"
    fi
    if [[ "$REQUIRE_NO_CPU_FALLBACK" == "1" && "$cpu_fallback_detected" != "0" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "cpu_fallback_detected")"
    fi
    if [[ "$REQUIRE_CPU_FALLBACK_DISABLED" == "1" && "$cpu_fallback_disabled" != "1" ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "cpu_fallback_gate_not_enforced")"
    fi
    if [[ "$error_msg" == *"fallback to CPU EP has been explicitly disabled"* ]]; then
      valid="0"
      valid_reason="$(append_reason "$valid_reason" "strict_cpu_ep_rejection")"
    fi

    if [[ "$valid" != "1" ]]; then
      any_invalid=1
      if [[ -z "$valid_reason" ]]; then
        valid_reason="invalid_unspecified"
      fi
    fi

    printf '%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$lane_name" "$clip_name" "$run_idx" "$duration_s" "$clip_fps" "$SUBMIT_FPS_MODE" "$submit_fps" "$warmup_frames" "$measured_frames" "$exit_code" "$success" "$valid" "$valid_reason" "$submitted" "$rejected" "$completed" "$wall_ms" "$submit_fps_out" "$complete_fps_out" "$infer_p50_ms" "$infer_p95_ms" "$infer_p99_ms" "$queue_p50_ms" "$queue_p95_ms" "$queue_p99_ms" "$e2e_p50_ms" "$e2e_p95_ms" "$e2e_p99_ms" "$health_ready" "$backend" "$requested_provider" "$active_provider" "$error_msg" "$run_log" \
      "$cpu_fallback_detected" "$cpu_fallback_disabled" "$coreml_coverage_known" "$coreml_supported_nodes" "$coreml_total_nodes" "$coreml_supported_partitions" "$coreml_coverage_ratio" \
      >> "$raw_tsv"
  done
done < <(resolve_clips)

summary_md="$OUTPUT_DIR/summary.md"
{
  echo "# Lenses Benchmark Contract v2 Summary"
  echo
  echo "- generated_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "- lane: $LANE"
  echo "- model: $MODEL_PATH"
  echo "- provider: $PROVIDER"
  echo "- expected_backend_gate: ${EXPECTED_BACKEND:-<none>}"
  echo "- expected_provider_gate: ${EXPECTED_PROVIDER:-<none>}"
  echo "- require_health_ready: $REQUIRE_HEALTH_READY"
  echo "- require_no_cpu_fallback: $REQUIRE_NO_CPU_FALLBACK"
  echo "- require_cpu_fallback_disabled: $REQUIRE_CPU_FALLBACK_DISABLED"
  echo "- input: ${INPUT_WIDTH}x${INPUT_HEIGHT}"
  echo "- target_fps: $TARGET_FPS"
  echo "- submit_fps_mode: $SUBMIT_FPS_MODE"
  echo "- warmup_seconds: $WARMUP_SECONDS"
  echo "- repeat_default: $REPEAT"
  echo "- repeat_fast_override: $REPEAT_FAST"
  echo "- repeat_full_override: $REPEAT_FULL"
  echo "- threshold_min_coreml_coverage_pct: ${THRESHOLD_MIN_COREML_COVERAGE_PCT:-<none>}"
  echo
  echo "## Runs"
  echo
  echo "| lane | clip | run | valid | valid_reason | measured_frames | warmup_frames | submit_fps | complete_fps | infer_p50 | infer_p95 | infer_p99 | queue_p95 | e2e_p95 | backend | requested_provider | active_provider | coreml_cov_pct |"
  echo "| --- | --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |"
  awk -F'\t' 'NR>1 {
    cov_pct = ($41+0.0) * 100.0
    printf("| %s | %s | %s | %s | %s | %s | %s | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %.3f | %s | %s | %s | %.2f |\n",
      $1, $2, $3, $12, ($13==""?"-":$13), $9, $8, $18+0.0, $19+0.0, $20+0.0, $21+0.0, $22+0.0, $24+0.0, $27+0.0, $30, $31, $32, cov_pct)
  }' "$raw_tsv"

  echo
  echo "## Aggregates (Valid Runs Only)"
  echo
  echo "| lane | runs_total | runs_valid | valid_rate | complete_fps_mean | complete_fps_stddev | complete_fps_ci95 | coreml_cov_mean_pct | infer_p95_mean | infer_p95_stddev | queue_p95_mean | e2e_p95_mean |"
  echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
  awk -F'\t' '
    function fmt_or_na(val, n) {
      if (n <= 0) return "n/a"
      return sprintf("%.3f", val)
    }
    function sample_stddev(n, sum, sumsq,  var) {
      if (n <= 1) return 0.0
      var = (sumsq - ((sum * sum) / n)) / (n - 1)
      if (var < 0) var = 0
      return sqrt(var)
    }
    function print_row(lane, n_total, n_valid, valid_rate, cf_mean, cf_std, cf_ci95, cov_mean_pct, inf_mean, inf_std, q_mean, e_mean) {
      printf("| %s | %d | %d | %.2f%% | %s | %s | %s | %s | %s | %s | %s | %s |\n",
        lane,
        n_total,
        n_valid,
        valid_rate,
        fmt_or_na(cf_mean, n_valid),
        fmt_or_na(cf_std, n_valid),
        fmt_or_na(cf_ci95, n_valid),
        fmt_or_na(cov_mean_pct, n_valid),
        fmt_or_na(inf_mean, n_valid),
        fmt_or_na(inf_std, n_valid),
        fmt_or_na(q_mean, n_valid),
        fmt_or_na(e_mean, n_valid))
    }
    NR>1 {
      lane=$1
      total[lane]++
      total_all++
      if ($12 == "1") {
        valid[lane]++
        valid_all++

        cf=$19+0.0
        inf=$21+0.0
        q=$24+0.0
        e=$27+0.0
        cov_pct=($41+0.0)*100.0

        cf_sum[lane]+=cf
        cf_sumsq[lane]+=cf*cf
        cov_sum[lane]+=cov_pct
        inf_sum[lane]+=inf
        inf_sumsq[lane]+=inf*inf
        q_sum[lane]+=q
        e_sum[lane]+=e

        cf_sum_all+=cf
        cf_sumsq_all+=cf*cf
        cov_sum_all+=cov_pct
        inf_sum_all+=inf
        inf_sumsq_all+=inf*inf
        q_sum_all+=q
        e_sum_all+=e
      }
    }
    END {
      for (lane in total) {
        n_total = total[lane]
        n_valid = valid[lane] + 0
        valid_rate = (n_total > 0 ? (100.0 * n_valid / n_total) : 0.0)
        cf_mean = (n_valid > 0 ? cf_sum[lane] / n_valid : 0.0)
        cf_std = sample_stddev(n_valid, cf_sum[lane], cf_sumsq[lane])
        cf_ci95 = (n_valid > 1 ? 1.96 * cf_std / sqrt(n_valid) : 0.0)
        cov_mean_pct = (n_valid > 0 ? cov_sum[lane] / n_valid : 0.0)
        inf_mean = (n_valid > 0 ? inf_sum[lane] / n_valid : 0.0)
        inf_std = sample_stddev(n_valid, inf_sum[lane], inf_sumsq[lane])
        q_mean = (n_valid > 0 ? q_sum[lane] / n_valid : 0.0)
        e_mean = (n_valid > 0 ? e_sum[lane] / n_valid : 0.0)
        print_row(lane, n_total, n_valid, valid_rate, cf_mean, cf_std, cf_ci95, cov_mean_pct, inf_mean, inf_std, q_mean, e_mean)
      }

      n_total = total_all + 0
      n_valid = valid_all + 0
      valid_rate = (n_total > 0 ? (100.0 * n_valid / n_total) : 0.0)
      cf_mean = (n_valid > 0 ? cf_sum_all / n_valid : 0.0)
      cf_std = sample_stddev(n_valid, cf_sum_all, cf_sumsq_all)
      cf_ci95 = (n_valid > 1 ? 1.96 * cf_std / sqrt(n_valid) : 0.0)
      cov_mean_pct = (n_valid > 0 ? cov_sum_all / n_valid : 0.0)
      inf_mean = (n_valid > 0 ? inf_sum_all / n_valid : 0.0)
      inf_std = sample_stddev(n_valid, inf_sum_all, inf_sumsq_all)
      q_mean = (n_valid > 0 ? q_sum_all / n_valid : 0.0)
      e_mean = (n_valid > 0 ? e_sum_all / n_valid : 0.0)
      print_row("overall", n_total, n_valid, valid_rate, cf_mean, cf_std, cf_ci95, cov_mean_pct, inf_mean, inf_std, q_mean, e_mean)
    }
  ' "$raw_tsv" | sort

  echo
  echo "## Steady-State Percentile Means (Valid Runs Only)"
  echo
  echo "| lane | infer_p50_mean | infer_p95_mean | infer_p99_mean | queue_p50_mean | queue_p95_mean | queue_p99_mean | e2e_p50_mean | e2e_p95_mean | e2e_p99_mean |"
  echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
  awk -F'\t' '
    function fmt_or_na(val, n) {
      if (n <= 0) return "n/a"
      return sprintf("%.3f", val)
    }
    function print_row(lane, n, inf50, inf95, inf99, q50, q95, q99, e50, e95, e99) {
      printf("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n",
        lane,
        fmt_or_na(inf50, n), fmt_or_na(inf95, n), fmt_or_na(inf99, n),
        fmt_or_na(q50, n), fmt_or_na(q95, n), fmt_or_na(q99, n),
        fmt_or_na(e50, n), fmt_or_na(e95, n), fmt_or_na(e99, n))
    }
    NR>1 && $12=="1" {
      lane=$1
      n[lane]++
      inf50_sum[lane]+=$20+0.0
      inf95_sum[lane]+=$21+0.0
      inf99_sum[lane]+=$22+0.0
      q50_sum[lane]+=$23+0.0
      q95_sum[lane]+=$24+0.0
      q99_sum[lane]+=$25+0.0
      e50_sum[lane]+=$26+0.0
      e95_sum[lane]+=$27+0.0
      e99_sum[lane]+=$28+0.0

      n_all++
      inf50_all+=$20+0.0
      inf95_all+=$21+0.0
      inf99_all+=$22+0.0
      q50_all+=$23+0.0
      q95_all+=$24+0.0
      q99_all+=$25+0.0
      e50_all+=$26+0.0
      e95_all+=$27+0.0
      e99_all+=$28+0.0
    }
    END {
      for (lane in n) {
        count=n[lane]
        print_row(lane, count,
          inf50_sum[lane]/count, inf95_sum[lane]/count, inf99_sum[lane]/count,
          q50_sum[lane]/count, q95_sum[lane]/count, q99_sum[lane]/count,
          e50_sum[lane]/count, e95_sum[lane]/count, e99_sum[lane]/count)
      }
      if (n_all > 0) {
        print_row("overall", n_all,
          inf50_all/n_all, inf95_all/n_all, inf99_all/n_all,
          q50_all/n_all, q95_all/n_all, q99_all/n_all,
          e50_all/n_all, e95_all/n_all, e99_all/n_all)
      } else {
        print_row("overall", 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
      }
    }
  ' "$raw_tsv" | sort

  invalid_count="$(awk -F'\t' 'NR>1 && $12!="1"{c++} END{print c+0}' "$raw_tsv")"
  if [[ "$invalid_count" != "0" ]]; then
    echo
    echo "## Invalid Runs"
    echo
    echo "| lane | clip | run | exit | success | health_ready | backend | requested_provider | active_provider | cpu_fallback_detected | cpu_fallback_disabled | coreml_cov_pct | valid_reason | error |"
    echo "| --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- | ---: | ---: | ---: | --- | --- |"
    awk -F'\t' 'NR>1 && $12!="1" {
      printf("| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %.2f | %s | %s |\n",
        $1, $2, $3, $10, $11, $29, $30, $31, $32, $35, $36, ($41+0.0)*100.0, $13, ($33==""?"-":$33))
    }' "$raw_tsv"
  fi

  echo
  echo "## Threshold Gates"
  echo

  overall_total="$(awk -F'\t' 'NR>1{c++} END{print c+0}' "$raw_tsv")"
  overall_valid="$(awk -F'\t' 'NR>1 && $12=="1"{c++} END{print c+0}' "$raw_tsv")"
  overall_valid_rate="$(awk -v v="$overall_valid" -v t="$overall_total" 'BEGIN{ if (t > 0) printf("%.6f", 100.0*v/t); else printf("0.000000") }')"
  overall_complete_mean="$(awk -F'\t' 'NR>1 && $12=="1"{s+=$19;n++} END{ if (n>0) printf("%.6f", s/n); else print "nan" }' "$raw_tsv")"
  overall_coreml_coverage_mean_pct="$(awk -F'\t' 'NR>1 && $12=="1"{s+=($41*100.0);n++} END{ if (n>0) printf("%.6f", s/n); else print "nan" }' "$raw_tsv")"
  overall_infer_p95_mean="$(awk -F'\t' 'NR>1 && $12=="1"{s+=$21;n++} END{ if (n>0) printf("%.6f", s/n); else print "nan" }' "$raw_tsv")"
  overall_queue_p95_mean="$(awk -F'\t' 'NR>1 && $12=="1"{s+=$24;n++} END{ if (n>0) printf("%.6f", s/n); else print "nan" }' "$raw_tsv")"
  overall_e2e_p95_mean="$(awk -F'\t' 'NR>1 && $12=="1"{s+=$27;n++} END{ if (n>0) printf("%.6f", s/n); else print "nan" }' "$raw_tsv")"

  threshold_fail=0

  check_min_gate() {
    local name="$1"
    local value="$2"
    local threshold="$3"
    if [[ -z "$threshold" ]]; then
      echo "- ${name}: SKIPPED (threshold not set)"
      return
    fi
    if [[ "$value" == "nan" ]]; then
      echo "- ${name}: FAIL (metric unavailable, threshold=${threshold})"
      threshold_fail=1
      return
    fi
    if awk -v v="$value" -v t="$threshold" 'BEGIN{exit !(v+0 >= t+0)}'; then
      echo "- ${name}: PASS (value=${value}, threshold=${threshold})"
    else
      echo "- ${name}: FAIL (value=${value}, threshold=${threshold})"
      threshold_fail=1
    fi
  }

  check_max_gate() {
    local name="$1"
    local value="$2"
    local threshold="$3"
    if [[ -z "$threshold" ]]; then
      echo "- ${name}: SKIPPED (threshold not set)"
      return
    fi
    if [[ "$value" == "nan" ]]; then
      echo "- ${name}: FAIL (metric unavailable, threshold=${threshold})"
      threshold_fail=1
      return
    fi
    if awk -v v="$value" -v t="$threshold" 'BEGIN{exit !(v+0 <= t+0)}'; then
      echo "- ${name}: PASS (value=${value}, threshold=${threshold})"
    else
      echo "- ${name}: FAIL (value=${value}, threshold=${threshold})"
      threshold_fail=1
    fi
  }

  check_min_gate "valid_rate_percent" "$overall_valid_rate" "$THRESHOLD_MIN_VALID_RATE"
  check_min_gate "complete_fps_mean" "$overall_complete_mean" "$THRESHOLD_MIN_COMPLETE_FPS"
  check_min_gate "coreml_coverage_mean_pct" "$overall_coreml_coverage_mean_pct" "$THRESHOLD_MIN_COREML_COVERAGE_PCT"
  check_max_gate "infer_p95_ms_mean" "$overall_infer_p95_mean" "$THRESHOLD_MAX_INFER_P95_MS"
  check_max_gate "queue_p95_ms_mean" "$overall_queue_p95_mean" "$THRESHOLD_MAX_QUEUE_P95_MS"
  check_max_gate "e2e_p95_ms_mean" "$overall_e2e_p95_mean" "$THRESHOLD_MAX_E2E_P95_MS"

  echo
  echo "- threshold_gate_failed: $threshold_fail"
} > "$summary_md"

echo "[lenses-lane-bench] Wrote summary: $summary_md"
echo "[lenses-lane-bench] Wrote raw rows: $raw_tsv"

threshold_failed="$(awk '/^- threshold_gate_failed:/{print $3}' "$summary_md" | tail -n 1)"
threshold_failed="${threshold_failed:-0}"

if [[ "$any_failures" == "1" && "$FAIL_ON_ERROR" == "1" ]]; then
  echo "[lenses-lane-bench] One or more runs failed (use summary artifacts for details)" >&2
  exit 1
fi
if [[ "$any_invalid" == "1" && "$FAIL_ON_INVALID" == "1" ]]; then
  echo "[lenses-lane-bench] One or more runs failed validity gates (see summary invalid section)" >&2
  exit 1
fi
if [[ "$threshold_failed" == "1" && "$FAIL_ON_THRESHOLD" == "1" ]]; then
  echo "[lenses-lane-bench] Threshold gates failed (see summary threshold section)" >&2
  exit 1
fi
