#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"

LANE="fast"
BUILD_DIR="build_macos"
CONFIG="RelWithDebInfo"
TARGET_FPS="30"
SUBMIT_FPS_MODE="target"
WARMUP_SECONDS="2"
REPEAT="1"
SKIP_BUILD="0"
OUTPUT_DIR=""
MODELS_ROOT="$ROOT_DIR/plugins/lenses/data/models"

usage() {
  cat <<USAGE
Usage: tools/lenses-bench/run-coreml-coverage-audit.sh [options]

Options:
  --lane <fast|full|all>           Benchmark lane to run per model (default: fast)
  --build-dir <dir>                Build directory (default: build_macos)
  --config <cfg>                   Build config (default: RelWithDebInfo)
  --target-fps <fps>               Target AI FPS (default: 30)
  --submit-fps-mode <mode>         clip|target|min (default: target)
  --warmup-seconds <sec>           Warmup seconds per run (default: 2)
  --repeat <n>                     Repeats per model (default: 1)
  --models-root <dir>              Model packages root (default: plugins/lenses/data/models)
  --output-dir <dir>               Output directory (default: bench/results/lenses-bench/coreml-coverage-<timestamp>)
  --skip-build                     Skip benchmark build
  -h, --help                       Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --lane) LANE="$2"; shift 2 ;;
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --config) CONFIG="$2"; shift 2 ;;
    --target-fps) TARGET_FPS="$2"; shift 2 ;;
    --submit-fps-mode) SUBMIT_FPS_MODE="$2"; shift 2 ;;
    --warmup-seconds) WARMUP_SECONDS="$2"; shift 2 ;;
    --repeat) REPEAT="$2"; shift 2 ;;
    --models-root) MODELS_ROOT="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --skip-build) SKIP_BUILD="1"; shift ;;
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

if [[ -z "$OUTPUT_DIR" ]]; then
  timestamp="$(date +%Y%m%d-%H%M%S)"
  OUTPUT_DIR="$ROOT_DIR/bench/results/lenses-bench/coreml-coverage-$timestamp"
fi

mkdir -p "$OUTPUT_DIR/runs"
summary_tsv="$OUTPUT_DIR/coverage-summary.tsv"
summary_md="$OUTPUT_DIR/coverage-summary.md"
printf 'model_id\tlane\truns_total\truns_ready\truns_coverage_known\tready_rate_pct\tcoreml_coverage_mean_pct\tcoreml_supported_nodes_mean\tcoreml_total_nodes_mean\tcoreml_partitions_mean\tcomplete_fps_mean\tinfer_p95_ms_mean\tsample_run_dir\n' > "$summary_tsv"

found_any=0
for model_dir in "$MODELS_ROOT"/*; do
  [[ -d "$model_dir" ]] || continue
  model_id="$(basename "$model_dir")"
  model_path="$model_dir/model.onnx"
  if [[ ! -f "$model_path" ]]; then
    continue
  fi
  found_any=1

  run_dir="$OUTPUT_DIR/runs/$model_id"
  mkdir -p "$run_dir"
  lane_output="$run_dir/lane"

  cmd=(
    "$ROOT_DIR/tools/lenses-bench/run-lane-bench.sh"
    --build-dir "$BUILD_DIR"
    --config "$CONFIG"
    --lane "$LANE"
    --model "$model_path"
    --provider "ort-coreml"
    --submit-fps-mode "$SUBMIT_FPS_MODE"
    --target-fps "$TARGET_FPS"
    --warmup-seconds "$WARMUP_SECONDS"
    --repeat "$REPEAT"
    --allow-fallback
    --no-require-no-cpu-fallback
    --no-require-cpu-fallback-disabled
    --expected-backend "ort"
    --expected-provider "ort-coreml"
    --output-dir "$lane_output"
  )
  if [[ "$SKIP_BUILD" == "1" ]]; then
    cmd+=(--skip-build)
  fi

  set +e
  "${cmd[@]}" >"$run_dir/audit.log" 2>&1
  run_exit=$?
  set -e
  if [[ "$run_exit" -ne 0 ]]; then
    echo "[coreml-coverage-audit] lane runner failed for $model_id (exit=$run_exit); keeping artifacts"
  fi

  rows_file="$lane_output/summary.tsv"
  if [[ ! -f "$rows_file" ]]; then
    echo "[coreml-coverage-audit] missing summary rows for $model_id" >&2
    continue
  fi

  runs_total="$(awk -F'\t' 'NR>1{c++} END{print c+0}' "$rows_file")"
  runs_ready="$(awk -F'\t' 'NR>1 && $29=="1"{c++} END{print c+0}' "$rows_file")"
  runs_coverage_known="$(awk -F'\t' 'NR>1 && $37=="1"{c++} END{print c+0}' "$rows_file")"
  ready_rate_pct="$(awk -v v="$runs_ready" -v t="$runs_total" 'BEGIN{ if (t>0) printf("%.3f", (100.0*v)/t); else print "0.000" }')"
  coverage_mean_pct="$(awk -F'\t' 'NR>1 && $29=="1" && $37=="1"{s+=($41*100.0);n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"
  supported_nodes_mean="$(awk -F'\t' 'NR>1 && $29=="1" && $37=="1"{s+=$38;n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"
  total_nodes_mean="$(awk -F'\t' 'NR>1 && $29=="1" && $37=="1"{s+=$39;n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"
  partitions_mean="$(awk -F'\t' 'NR>1 && $29=="1" && $37=="1"{s+=$40;n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"
  complete_fps_mean="$(awk -F'\t' 'NR>1 && $29=="1"{s+=$19;n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"
  infer_p95_mean="$(awk -F'\t' 'NR>1 && $29=="1"{s+=$21;n++} END{ if (n>0) printf("%.3f", s/n); else print "nan" }' "$rows_file")"

  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$model_id" "$LANE" "$runs_total" "$runs_ready" "$runs_coverage_known" "$ready_rate_pct" \
    "$coverage_mean_pct" "$supported_nodes_mean" "$total_nodes_mean" "$partitions_mean" \
    "$complete_fps_mean" "$infer_p95_mean" "$lane_output" \
    >> "$summary_tsv"
done

if [[ "$found_any" != "1" ]]; then
  echo "No model packages with model.onnx found under: $MODELS_ROOT" >&2
  exit 1
fi

{
  echo "# CoreML Coverage Audit"
  echo
  echo "- generated_at: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "- lane: $LANE"
  echo "- target_fps: $TARGET_FPS"
  echo "- submit_fps_mode: $SUBMIT_FPS_MODE"
  echo "- warmup_seconds: $WARMUP_SECONDS"
  echo "- repeat: $REPEAT"
  echo
  echo "| model | ready_rate_pct | coreml_coverage_mean_pct | complete_fps_mean | infer_p95_ms_mean | supported_nodes_mean | total_nodes_mean | partitions_mean | sample_run_dir |"
  echo "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |"
  awk -F'\t' 'NR>1 {
    printf("%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", $1, $6, $7, $11, $12, $8, $9, $10, $13)
  }' "$summary_tsv" | sort -t $'\t' -k3,3nr -k4,4nr | awk -F'\t' '{
    printf("| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, $5, $6, $7, $8, $9)
  }'
} > "$summary_md"

echo "[coreml-coverage-audit] Wrote: $summary_md"
echo "[coreml-coverage-audit] Wrote: $summary_tsv"
