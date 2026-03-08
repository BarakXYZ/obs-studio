#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
DEFAULT_DB="$ROOT_DIR/bench/results/lenses-bench/history/history.sqlite"
DEFAULT_RETENTION=100

usage() {
  cat <<USAGE
Usage: tools/lenses-bench/contract-history.sh <command> [options]

Commands:
  register   Register a contract-v2 run directory into local history DB
  list       List recent registered runs
  compare    Compare two registered runs (or auto-previous)

register options:
  --run-dir <dir>             Run directory containing summary.md + summary.tsv (required)
  --history-db <path>         History sqlite DB path (default: bench/results/lenses-bench/history/history.sqlite)
  --retention <n>             Keep latest N runs in DB (default: 100)
  --run-id <id>               Explicit run id override (default: basename of run-dir)
  --prune-run-dirs            Delete run directories pruned by retention policy

list options:
  --history-db <path>         History sqlite DB path
  --limit <n>                 Number of rows (default: 20)

compare options:
  --history-db <path>         History sqlite DB path
  --new <run-id|latest|path>  New run selector (default: latest)
  --old <selector>            Old run selector (default: previous strict-compatible)
                              selectors:
                                previous      strict profile-compatible previous run
                                previous-any  any previous run by time
                                days:<n>      strict profile-compatible run at least N days older
                                latest        latest run in DB
                                <run-id|path> explicit run id or run directory
  --output <path>             Write markdown report to this file
USAGE
}

sql_escape() {
  local value="$1"
  value="${value//\'/\'\'}"
  printf '%s' "$value"
}

abs_path() {
  local path="$1"
  if [[ -d "$path" ]]; then
    (cd "$path" && pwd)
  else
    local parent
    parent="$(cd "$(dirname "$path")" && pwd)"
    printf '%s/%s' "$parent" "$(basename "$path")"
  fi
}

extract_meta_value() {
  local summary_md="$1"
  local key="$2"
  awk -F': ' -v k="$key" '$0 ~ ("^- " k ": ") { sub("^- " k ": ", "", $0); print; exit }' "$summary_md"
}

init_db() {
  local db_path="$1"
  mkdir -p "$(dirname "$db_path")"
  sqlite3 "$db_path" > /dev/null <<'SQL'
PRAGMA foreign_keys = ON;
PRAGMA journal_mode = WAL;
PRAGMA busy_timeout = 5000;
CREATE TABLE IF NOT EXISTS runs (
  run_id TEXT PRIMARY KEY,
  created_at_utc TEXT NOT NULL,
  run_dir TEXT NOT NULL UNIQUE,
  lane TEXT,
  model_path TEXT,
  provider TEXT,
  input_wh TEXT,
  target_fps REAL,
  submit_fps_mode TEXT,
  warmup_seconds REAL,
  repeat_default INTEGER,
  repeat_fast INTEGER,
  repeat_full INTEGER,
  git_branch TEXT,
  git_commit TEXT,
  host_name TEXT,
  registered_at_utc TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS case_metrics (
  run_id TEXT NOT NULL,
  lane TEXT NOT NULL,
  clip TEXT NOT NULL,
  run_index INTEGER NOT NULL,
  valid INTEGER NOT NULL,
  valid_reason TEXT,
  measured_frames INTEGER,
  warmup_frames INTEGER,
  submit_fps REAL,
  complete_fps REAL,
  infer_p50_ms REAL,
  infer_p95_ms REAL,
  infer_p99_ms REAL,
  queue_p50_ms REAL,
  queue_p95_ms REAL,
  queue_p99_ms REAL,
  e2e_p50_ms REAL,
  e2e_p95_ms REAL,
  e2e_p99_ms REAL,
  exit_code INTEGER,
  success INTEGER,
  health_ready INTEGER,
  backend TEXT,
  active_provider TEXT,
  error TEXT,
  log_path TEXT,
  PRIMARY KEY (run_id, lane, clip, run_index),
  FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS aggregates (
  run_id TEXT NOT NULL,
  lane TEXT NOT NULL,
  runs_total INTEGER NOT NULL,
  runs_valid INTEGER NOT NULL,
  valid_rate REAL NOT NULL,
  complete_fps_mean REAL,
  infer_p50_mean REAL,
  infer_p95_mean REAL,
  infer_p99_mean REAL,
  queue_p50_mean REAL,
  queue_p95_mean REAL,
  queue_p99_mean REAL,
  e2e_p50_mean REAL,
  e2e_p95_mean REAL,
  e2e_p99_mean REAL,
  PRIMARY KEY (run_id, lane),
  FOREIGN KEY (run_id) REFERENCES runs(run_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS runs_created_idx ON runs(created_at_utc DESC);
CREATE INDEX IF NOT EXISTS case_run_idx ON case_metrics(run_id);
CREATE INDEX IF NOT EXISTS aggr_run_idx ON aggregates(run_id);
SQL
}

register_run() {
  local db_path="$DEFAULT_DB"
  local run_dir=""
  local retention="$DEFAULT_RETENTION"
  local run_id_override=""
  local prune_run_dirs="0"

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --run-dir) run_dir="$2"; shift 2 ;;
      --history-db) db_path="$2"; shift 2 ;;
      --retention) retention="$2"; shift 2 ;;
      --run-id) run_id_override="$2"; shift 2 ;;
      --prune-run-dirs) prune_run_dirs="1"; shift ;;
      *)
        echo "Unknown register option: $1" >&2
        exit 1
        ;;
    esac
  done

  if [[ -z "$run_dir" ]]; then
    echo "register requires --run-dir" >&2
    exit 1
  fi

  run_dir="$(abs_path "$run_dir")"
  local summary_md="$run_dir/summary.md"
  local summary_tsv="$run_dir/summary.tsv"

  if [[ ! -f "$summary_md" ]]; then
    echo "Missing summary.md in run dir: $run_dir" >&2
    exit 1
  fi
  if [[ ! -f "$summary_tsv" ]]; then
    echo "Missing summary.tsv in run dir: $run_dir" >&2
    exit 1
  fi

  init_db "$db_path"

  local run_id
  if [[ -n "$run_id_override" ]]; then
    run_id="$run_id_override"
  else
    run_id="$(basename "$run_dir")"
  fi

  local created_at
  created_at="$(extract_meta_value "$summary_md" "generated_at")"
  if [[ -z "$created_at" ]]; then
    created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  fi

  local lane model_path provider input_wh target_fps submit_fps_mode warmup_seconds repeat_default repeat_fast repeat_full
  lane="$(extract_meta_value "$summary_md" "lane")"
  model_path="$(extract_meta_value "$summary_md" "model")"
  provider="$(extract_meta_value "$summary_md" "provider")"
  input_wh="$(extract_meta_value "$summary_md" "input")"
  target_fps="$(extract_meta_value "$summary_md" "target_fps")"
  submit_fps_mode="$(extract_meta_value "$summary_md" "submit_fps_mode")"
  warmup_seconds="$(extract_meta_value "$summary_md" "warmup_seconds")"
  repeat_default="$(extract_meta_value "$summary_md" "repeat_default")"
  repeat_fast="$(extract_meta_value "$summary_md" "repeat_fast_override")"
  repeat_full="$(extract_meta_value "$summary_md" "repeat_full_override")"

  local git_branch git_commit host_name
  git_branch="$(git -C "$ROOT_DIR" rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
  git_commit="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
  host_name="$(hostname 2>/dev/null || echo unknown)"

  local e_run_id e_created_at e_run_dir e_lane e_model_path e_provider e_input_wh e_submit_fps_mode e_git_branch e_git_commit e_host_name
  e_run_id="$(sql_escape "$run_id")"
  e_created_at="$(sql_escape "$created_at")"
  e_run_dir="$(sql_escape "$run_dir")"
  e_lane="$(sql_escape "$lane")"
  e_model_path="$(sql_escape "$model_path")"
  e_provider="$(sql_escape "$provider")"
  e_input_wh="$(sql_escape "$input_wh")"
  e_submit_fps_mode="$(sql_escape "$submit_fps_mode")"
  e_git_branch="$(sql_escape "$git_branch")"
  e_git_commit="$(sql_escape "$git_commit")"
  e_host_name="$(sql_escape "$host_name")"

  sqlite3 "$db_path" <<SQL
PRAGMA foreign_keys = ON;
DELETE FROM aggregates WHERE run_id='$e_run_id';
DELETE FROM case_metrics WHERE run_id='$e_run_id';
DELETE FROM runs WHERE run_id='$e_run_id';
INSERT INTO runs (
  run_id, created_at_utc, run_dir, lane, model_path, provider, input_wh,
  target_fps, submit_fps_mode, warmup_seconds,
  repeat_default, repeat_fast, repeat_full,
  git_branch, git_commit, host_name, registered_at_utc
) VALUES (
  '$e_run_id', '$e_created_at', '$e_run_dir', '$e_lane', '$e_model_path', '$e_provider', '$e_input_wh',
  ${target_fps:-0}, '$e_submit_fps_mode', ${warmup_seconds:-0},
  ${repeat_default:-0}, ${repeat_fast:-0}, ${repeat_full:-0},
  '$e_git_branch', '$e_git_commit', '$e_host_name', datetime('now')
);
SQL

  local e_summary_tsv
  e_summary_tsv="$(sql_escape "$summary_tsv")"

  sqlite3 "$db_path" <<SQL
PRAGMA foreign_keys = ON;
DROP TABLE IF EXISTS temp.raw_case_metrics;
CREATE TEMP TABLE raw_case_metrics (
  lane TEXT,
  clip TEXT,
  repeat TEXT,
  duration_s TEXT,
  clip_fps TEXT,
  submit_fps_mode TEXT,
  submit_fps TEXT,
  warmup_frames TEXT,
  measured_frames TEXT,
  exit_code TEXT,
  success TEXT,
  valid TEXT,
  valid_reason TEXT,
  submitted TEXT,
  rejected TEXT,
  completed TEXT,
  wall_ms TEXT,
  submit_fps_out TEXT,
  complete_fps TEXT,
  infer_p50_ms TEXT,
  infer_p95_ms TEXT,
  infer_p99_ms TEXT,
  queue_p50_ms TEXT,
  queue_p95_ms TEXT,
  queue_p99_ms TEXT,
  e2e_p50_ms TEXT,
  e2e_p95_ms TEXT,
  e2e_p99_ms TEXT,
  health_ready TEXT,
  backend TEXT,
  requested_provider TEXT,
  active_provider TEXT,
  error TEXT,
  log_path TEXT,
  cpu_fallback_detected TEXT,
  cpu_fallback_disabled TEXT,
  coreml_coverage_known TEXT,
  coreml_supported_nodes TEXT,
  coreml_total_nodes TEXT,
  coreml_supported_partitions TEXT,
  coreml_coverage_ratio TEXT
);
.mode tabs
.import '$e_summary_tsv' raw_case_metrics
DELETE FROM raw_case_metrics WHERE lane='lane';

INSERT INTO case_metrics (
  run_id, lane, clip, run_index, valid, valid_reason,
  measured_frames, warmup_frames, submit_fps, complete_fps,
  infer_p50_ms, infer_p95_ms, infer_p99_ms,
  queue_p50_ms, queue_p95_ms, queue_p99_ms,
  e2e_p50_ms, e2e_p95_ms, e2e_p99_ms,
  exit_code, success, health_ready, backend, active_provider, error, log_path
)
SELECT
  '$e_run_id',
  lane,
  clip,
  CAST(COALESCE(NULLIF(repeat, ''), '0') AS INTEGER),
  CAST(COALESCE(NULLIF(valid, ''), '0') AS INTEGER),
  NULLIF(valid_reason, ''),
  CAST(COALESCE(NULLIF(measured_frames, ''), '0') AS INTEGER),
  CAST(COALESCE(NULLIF(warmup_frames, ''), '0') AS INTEGER),
  CAST(COALESCE(NULLIF(submit_fps_out, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(complete_fps, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(infer_p50_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(infer_p95_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(infer_p99_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(queue_p50_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(queue_p95_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(queue_p99_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(e2e_p50_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(e2e_p95_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(e2e_p99_ms, ''), '0') AS REAL),
  CAST(COALESCE(NULLIF(exit_code, ''), '0') AS INTEGER),
  CAST(COALESCE(NULLIF(success, ''), '0') AS INTEGER),
  CAST(COALESCE(NULLIF(health_ready, ''), '0') AS INTEGER),
  NULLIF(backend, ''),
  COALESCE(NULLIF(active_provider, ''), NULLIF(requested_provider, '')),
  NULLIF(error, ''),
  NULLIF(log_path, '')
FROM raw_case_metrics;
SQL

  sqlite3 "$db_path" <<SQL
INSERT INTO aggregates (
  run_id, lane, runs_total, runs_valid, valid_rate,
  complete_fps_mean,
  infer_p50_mean, infer_p95_mean, infer_p99_mean,
  queue_p50_mean, queue_p95_mean, queue_p99_mean,
  e2e_p50_mean, e2e_p95_mean, e2e_p99_mean
)
SELECT
  '$e_run_id', lane,
  COUNT(*) AS runs_total,
  SUM(CASE WHEN valid=1 THEN 1 ELSE 0 END) AS runs_valid,
  CASE WHEN COUNT(*) > 0 THEN 100.0 * SUM(CASE WHEN valid=1 THEN 1 ELSE 0 END) / COUNT(*) ELSE 0.0 END AS valid_rate,
  AVG(CASE WHEN valid=1 THEN complete_fps END) AS complete_fps_mean,
  AVG(CASE WHEN valid=1 THEN infer_p50_ms END) AS infer_p50_mean,
  AVG(CASE WHEN valid=1 THEN infer_p95_ms END) AS infer_p95_mean,
  AVG(CASE WHEN valid=1 THEN infer_p99_ms END) AS infer_p99_mean,
  AVG(CASE WHEN valid=1 THEN queue_p50_ms END) AS queue_p50_mean,
  AVG(CASE WHEN valid=1 THEN queue_p95_ms END) AS queue_p95_mean,
  AVG(CASE WHEN valid=1 THEN queue_p99_ms END) AS queue_p99_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p50_ms END) AS e2e_p50_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p95_ms END) AS e2e_p95_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p99_ms END) AS e2e_p99_mean
FROM case_metrics
WHERE run_id='$e_run_id'
GROUP BY lane;

INSERT INTO aggregates (
  run_id, lane, runs_total, runs_valid, valid_rate,
  complete_fps_mean,
  infer_p50_mean, infer_p95_mean, infer_p99_mean,
  queue_p50_mean, queue_p95_mean, queue_p99_mean,
  e2e_p50_mean, e2e_p95_mean, e2e_p99_mean
)
SELECT
  '$e_run_id', 'overall',
  COUNT(*) AS runs_total,
  SUM(CASE WHEN valid=1 THEN 1 ELSE 0 END) AS runs_valid,
  CASE WHEN COUNT(*) > 0 THEN 100.0 * SUM(CASE WHEN valid=1 THEN 1 ELSE 0 END) / COUNT(*) ELSE 0.0 END AS valid_rate,
  AVG(CASE WHEN valid=1 THEN complete_fps END) AS complete_fps_mean,
  AVG(CASE WHEN valid=1 THEN infer_p50_ms END) AS infer_p50_mean,
  AVG(CASE WHEN valid=1 THEN infer_p95_ms END) AS infer_p95_mean,
  AVG(CASE WHEN valid=1 THEN infer_p99_ms END) AS infer_p99_mean,
  AVG(CASE WHEN valid=1 THEN queue_p50_ms END) AS queue_p50_mean,
  AVG(CASE WHEN valid=1 THEN queue_p95_ms END) AS queue_p95_mean,
  AVG(CASE WHEN valid=1 THEN queue_p99_ms END) AS queue_p99_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p50_ms END) AS e2e_p50_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p95_ms END) AS e2e_p95_mean,
  AVG(CASE WHEN valid=1 THEN e2e_p99_ms END) AS e2e_p99_mean
FROM case_metrics
WHERE run_id='$e_run_id';
SQL

  local prune_rows
  prune_rows="$(sqlite3 -separator $'\t' "$db_path" "SELECT run_id, run_dir FROM runs ORDER BY created_at_utc DESC LIMIT -1 OFFSET ${retention};")"
  if [[ -n "$prune_rows" ]]; then
    while IFS=$'\t' read -r prune_id prune_dir; do
      [[ -z "$prune_id" ]] && continue
      local e_prune_id
      e_prune_id="$(sql_escape "$prune_id")"
      sqlite3 "$db_path" "DELETE FROM runs WHERE run_id='$e_prune_id';"
      if [[ "$prune_run_dirs" == "1" && -n "$prune_dir" && -d "$prune_dir" ]]; then
        rm -rf "$prune_dir"
      fi
    done <<< "$prune_rows"
  fi

  printf '%s\n' "$run_id"
}

resolve_run_id() {
  local db_path="$1"
  local selector="$2"

  if [[ -z "$selector" || "$selector" == "latest" ]]; then
    sqlite3 "$db_path" "SELECT run_id FROM runs ORDER BY created_at_utc DESC LIMIT 1;"
    return
  fi

  if [[ -d "$selector" ]]; then
    local abs_sel e_abs_sel
    abs_sel="$(abs_path "$selector")"
    e_abs_sel="$(sql_escape "$abs_sel")"
    sqlite3 "$db_path" "SELECT run_id FROM runs WHERE run_dir='$e_abs_sel' LIMIT 1;"
    return
  fi

  printf '%s' "$selector"
}

find_previous_profile_run() {
  local db_path="$1"
  local new_id="$2"
  local e_new
  e_new="$(sql_escape "$new_id")"

  sqlite3 "$db_path" <<SQL
WITH src AS (
  SELECT
    lane,
    model_path,
    provider,
    input_wh,
    IFNULL(target_fps, 0) AS target_fps,
    IFNULL(submit_fps_mode, '') AS submit_fps_mode,
    julianday(replace(replace(created_at_utc, 'T', ' '), 'Z', '')) AS src_jd
  FROM runs
  WHERE run_id = '$e_new'
)
SELECT run_id
FROM runs r2, src s
WHERE r2.run_id != '$e_new'
  AND r2.lane = s.lane
  AND r2.model_path = s.model_path
  AND r2.provider = s.provider
  AND r2.input_wh = s.input_wh
  AND IFNULL(r2.target_fps, 0) = s.target_fps
  AND IFNULL(r2.submit_fps_mode, '') = s.submit_fps_mode
  AND julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) < s.src_jd
ORDER BY julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) DESC
LIMIT 1;
SQL
}

find_previous_any_run() {
  local db_path="$1"
  local new_id="$2"
  local e_new
  e_new="$(sql_escape "$new_id")"

  sqlite3 "$db_path" <<SQL
WITH src AS (
  SELECT julianday(replace(replace(created_at_utc, 'T', ' '), 'Z', '')) AS src_jd
  FROM runs
  WHERE run_id = '$e_new'
)
SELECT r2.run_id
FROM runs r2, src s
WHERE r2.run_id != '$e_new'
  AND julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) < s.src_jd
ORDER BY julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) DESC
LIMIT 1;
SQL
}

find_profile_run_days_ago() {
  local db_path="$1"
  local new_id="$2"
  local days="$3"
  local e_new
  e_new="$(sql_escape "$new_id")"

  sqlite3 "$db_path" <<SQL
WITH src AS (
  SELECT
    lane,
    model_path,
    provider,
    input_wh,
    IFNULL(target_fps, 0) AS target_fps,
    IFNULL(submit_fps_mode, '') AS submit_fps_mode,
    julianday(replace(replace(created_at_utc, 'T', ' '), 'Z', '')) AS src_jd
  FROM runs
  WHERE run_id = '$e_new'
)
SELECT r2.run_id
FROM runs r2, src s
WHERE r2.run_id != '$e_new'
  AND r2.lane = s.lane
  AND r2.model_path = s.model_path
  AND r2.provider = s.provider
  AND r2.input_wh = s.input_wh
  AND IFNULL(r2.target_fps, 0) = s.target_fps
  AND IFNULL(r2.submit_fps_mode, '') = s.submit_fps_mode
  AND julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) <= (s.src_jd - ${days})
ORDER BY julianday(replace(replace(r2.created_at_utc, 'T', ' '), 'Z', '')) DESC
LIMIT 1;
SQL
}

list_runs() {
  local db_path="$DEFAULT_DB"
  local limit=20

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --history-db) db_path="$2"; shift 2 ;;
      --limit) limit="$2"; shift 2 ;;
      *)
        echo "Unknown list option: $1" >&2
        exit 1
        ;;
    esac
  done

  init_db "$db_path"

  {
    echo "| run_id | created_at_utc | lane | provider | target_fps | valid_rate | complete_fps_mean | infer_p95_mean | run_dir |"
    echo "| --- | --- | --- | --- | ---: | ---: | ---: | ---: | --- |"
    sqlite3 -separator $'\t' "$db_path" "
SELECT r.run_id,
       r.created_at_utc,
       r.lane,
       r.provider,
       IFNULL(r.target_fps, 0),
       IFNULL(a.valid_rate, 0),
       IFNULL(a.complete_fps_mean, 0),
       IFNULL(a.infer_p95_mean, 0),
       r.run_dir
FROM runs r
LEFT JOIN aggregates a ON a.run_id = r.run_id AND a.lane='overall'
ORDER BY r.created_at_utc DESC
LIMIT ${limit};" | while IFS=$'\t' read -r run_id created lane provider target valid complete infer run_dir; do
      echo "| ${run_id} | ${created} | ${lane} | ${provider} | ${target} | ${valid} | ${complete} | ${infer} | ${run_dir} |"
    done
  }
}

is_number() {
  local value="$1"
  awk -v v="$value" 'BEGIN { if (v ~ /^-?[0-9]+([.][0-9]+)?$/) exit 0; exit 1 }'
}

delta_value() {
  local old="$1"
  local new="$2"
  if ! is_number "$old" || ! is_number "$new"; then
    printf 'n/a'
    return
  fi
  awk -v o="$old" -v n="$new" 'BEGIN { printf("%.3f", n - o) }'
}

delta_percent() {
  local old="$1"
  local new="$2"
  if ! is_number "$old" || ! is_number "$new"; then
    printf 'n/a'
    return
  fi
  awk -v o="$old" -v n="$new" 'BEGIN {
    if (o == 0) { printf("n/a"); exit }
    printf("%.2f%%", ((n - o) / o) * 100.0)
  }'
}

trend_higher_better() {
  local old="$1"
  local new="$2"
  if ! is_number "$old" || ! is_number "$new"; then
    printf 'n/a'
    return
  fi
  awk -v o="$old" -v n="$new" 'BEGIN {
    if (n > o) print "improved";
    else if (n < o) print "regressed";
    else print "flat";
  }'
}

trend_lower_better() {
  local old="$1"
  local new="$2"
  if ! is_number "$old" || ! is_number "$new"; then
    printf 'n/a'
    return
  fi
  awk -v o="$old" -v n="$new" 'BEGIN {
    if (n < o) print "improved";
    else if (n > o) print "regressed";
    else print "flat";
  }'
}

fetch_run_meta() {
  local db_path="$1"
  local run_id="$2"
  local e_run_id
  e_run_id="$(sql_escape "$run_id")"
  sqlite3 -separator $'\t' "$db_path" "SELECT run_id, created_at_utc, lane, model_path, provider, input_wh, target_fps, submit_fps_mode, warmup_seconds, run_dir FROM runs WHERE run_id='$e_run_id' LIMIT 1;"
}

fetch_lane_metrics() {
  local db_path="$1"
  local run_id="$2"
  local lane="$3"
  local e_run_id e_lane
  e_run_id="$(sql_escape "$run_id")"
  e_lane="$(sql_escape "$lane")"
  sqlite3 -separator $'\t' "$db_path" "SELECT runs_total, runs_valid, valid_rate, complete_fps_mean, infer_p95_mean, queue_p95_mean, e2e_p95_mean FROM aggregates WHERE run_id='$e_run_id' AND lane='$e_lane' LIMIT 1;"
}

compare_runs() {
  local db_path="$DEFAULT_DB"
  local new_selector="latest"
  local old_selector="previous"
  local output_path=""

  while [[ $# -gt 0 ]]; do
    case "$1" in
      --history-db) db_path="$2"; shift 2 ;;
      --new) new_selector="$2"; shift 2 ;;
      --old) old_selector="$2"; shift 2 ;;
      --output) output_path="$2"; shift 2 ;;
      *)
        echo "Unknown compare option: $1" >&2
        exit 1
        ;;
    esac
  done

  init_db "$db_path"

  local new_id
  new_id="$(resolve_run_id "$db_path" "$new_selector")"
  if [[ -z "$new_id" ]]; then
    echo "Unable to resolve --new run selector: $new_selector" >&2
    exit 1
  fi

  local old_id=""
  if [[ "$old_selector" == "previous" ]]; then
    old_id="$(find_previous_profile_run "$db_path" "$new_id")"
  elif [[ "$old_selector" == "previous-any" ]]; then
    old_id="$(find_previous_any_run "$db_path" "$new_id")"
  elif [[ "$old_selector" == days:* ]]; then
    local days_value="${old_selector#days:}"
    if ! [[ "$days_value" =~ ^[0-9]+$ ]]; then
      echo "Invalid --old days selector: $old_selector (expected days:<non-negative-int>)" >&2
      exit 1
    fi
    old_id="$(find_profile_run_days_ago "$db_path" "$new_id" "$days_value")"
  else
    old_id="$(resolve_run_id "$db_path" "$old_selector")"
  fi

  if [[ -z "$old_id" ]]; then
    echo "Unable to resolve comparison baseline (old run). For broad fallback use --old previous-any." >&2
    exit 1
  fi

  local new_meta old_meta
  new_meta="$(fetch_run_meta "$db_path" "$new_id")"
  old_meta="$(fetch_run_meta "$db_path" "$old_id")"

  if [[ -z "$new_meta" || -z "$old_meta" ]]; then
    echo "Failed to load run metadata for comparison" >&2
    exit 1
  fi

  local report
  report="$(mktemp)"

  {
    echo "# Benchmark Comparison"
    echo
    echo "- new_run: $new_id"
    echo "- old_run: $old_id"
    echo
    echo "## Run Metadata"
    echo
    echo "| side | run_id | created_at_utc | lane | provider | target_fps | submit_fps_mode | input | run_dir |"
    echo "| --- | --- | --- | --- | --- | ---: | --- | --- | --- |"

    local run_id created lane model provider input target submit_mode warmup run_dir
    IFS=$'\t' read -r run_id created lane model provider input target submit_mode warmup run_dir <<< "$old_meta"
    echo "| old | $run_id | $created | $lane | $provider | $target | $submit_mode | $input | $run_dir |"
    IFS=$'\t' read -r run_id created lane model provider input target submit_mode warmup run_dir <<< "$new_meta"
    echo "| new | $run_id | $created | $lane | $provider | $target | $submit_mode | $input | $run_dir |"

    echo
    echo "## Metrics Delta"
    echo
    echo "| lane | metric | old | new | delta | delta_pct | trend |"
    echo "| --- | --- | ---: | ---: | ---: | ---: | --- |"

    local lanes
    lanes="$(sqlite3 "$db_path" "SELECT DISTINCT lane FROM aggregates WHERE run_id IN ('$(sql_escape "$new_id")','$(sql_escape "$old_id")') ORDER BY CASE lane WHEN 'overall' THEN 0 ELSE 1 END, lane;")"

    while IFS= read -r lane_name; do
      [[ -z "$lane_name" ]] && continue

      local old_row new_row
      old_row="$(fetch_lane_metrics "$db_path" "$old_id" "$lane_name")"
      new_row="$(fetch_lane_metrics "$db_path" "$new_id" "$lane_name")"

      local old_runs_total old_runs_valid old_valid old_complete old_infer old_queue old_e2e
      local new_runs_total new_runs_valid new_valid new_complete new_infer new_queue new_e2e

      if [[ -n "$old_row" ]]; then
        IFS=$'\t' read -r old_runs_total old_runs_valid old_valid old_complete old_infer old_queue old_e2e <<< "$old_row"
      else
        old_runs_total="0"; old_runs_valid="0"; old_valid=""; old_complete=""; old_infer=""; old_queue=""; old_e2e=""
      fi

      if [[ -n "$new_row" ]]; then
        IFS=$'\t' read -r new_runs_total new_runs_valid new_valid new_complete new_infer new_queue new_e2e <<< "$new_row"
      else
        new_runs_total="0"; new_runs_valid="0"; new_valid=""; new_complete=""; new_infer=""; new_queue=""; new_e2e=""
      fi

      echo "| $lane_name | valid_rate | ${old_valid:-n/a} | ${new_valid:-n/a} | $(delta_value "${old_valid:-}" "${new_valid:-}") | $(delta_percent "${old_valid:-}" "${new_valid:-}") | $(trend_higher_better "${old_valid:-}" "${new_valid:-}") |"
      echo "| $lane_name | complete_fps_mean | ${old_complete:-n/a} | ${new_complete:-n/a} | $(delta_value "${old_complete:-}" "${new_complete:-}") | $(delta_percent "${old_complete:-}" "${new_complete:-}") | $(trend_higher_better "${old_complete:-}" "${new_complete:-}") |"
      echo "| $lane_name | infer_p95_mean_ms | ${old_infer:-n/a} | ${new_infer:-n/a} | $(delta_value "${old_infer:-}" "${new_infer:-}") | $(delta_percent "${old_infer:-}" "${new_infer:-}") | $(trend_lower_better "${old_infer:-}" "${new_infer:-}") |"
      echo "| $lane_name | queue_p95_mean_ms | ${old_queue:-n/a} | ${new_queue:-n/a} | $(delta_value "${old_queue:-}" "${new_queue:-}") | $(delta_percent "${old_queue:-}" "${new_queue:-}") | $(trend_lower_better "${old_queue:-}" "${new_queue:-}") |"
      echo "| $lane_name | e2e_p95_mean_ms | ${old_e2e:-n/a} | ${new_e2e:-n/a} | $(delta_value "${old_e2e:-}" "${new_e2e:-}") | $(delta_percent "${old_e2e:-}" "${new_e2e:-}") | $(trend_lower_better "${old_e2e:-}" "${new_e2e:-}") |"
    done <<< "$lanes"
  } > "$report"

  if [[ -n "$output_path" ]]; then
    mkdir -p "$(dirname "$output_path")"
    cp "$report" "$output_path"
  fi

  cat "$report"
  rm -f "$report"
}

main() {
  if [[ $# -lt 1 ]]; then
    usage
    exit 1
  fi

  local cmd="$1"
  shift

  case "$cmd" in
    register)
      register_run "$@"
      ;;
    list)
      list_runs "$@"
      ;;
    compare)
      compare_runs "$@"
      ;;
    -h|--help|help)
      usage
      ;;
    *)
      echo "Unknown command: $cmd" >&2
      usage
      exit 1
      ;;
  esac
}

main "$@"
