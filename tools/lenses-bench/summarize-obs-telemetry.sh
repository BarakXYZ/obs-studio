#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <obs-log-path>" >&2
  exit 1
fi

LOG_PATH="$1"
if [[ ! -f "$LOG_PATH" ]]; then
  echo "Missing log file: $LOG_PATH" >&2
  exit 1
fi

awk '
  /\[lenses\] debug telemetry/ {
    mode = 0
    for (i = 1; i <= NF; i++) {
      if ($i ~ /^submit_fps=/) {
        split($i,a,"="); s += a[2]; sc++;
        if (sc == 1 || a[2] < smin) smin = a[2];
        if (sc == 1 || a[2] > smax) smax = a[2];
      }
      if ($i ~ /^complete_fps=/) {
        split($i,a,"="); c += a[2]; cc++;
        if (cc == 1 || a[2] < cmin) cmin = a[2];
        if (cc == 1 || a[2] > cmax) cmax = a[2];
      }
      if ($i ~ /^drop_fps=/) {
        split($i,a,"="); d += a[2]; dc++;
        if (dc == 1 || a[2] < dmin) dmin = a[2];
        if (dc == 1 || a[2] > dmax) dmax = a[2];
      }
      if ($i ~ /^stage_ms\(last:/) mode = 1;
      if ($i ~ /^stage_p95\(readback=/) mode = 2;
      if ($i ~ /^infer=/ && mode > 0) {
        split($i,a,"=");
        gsub(/[^0-9.\-]/, "", a[2]);
        if (a[2] != "") {
          v = a[2] + 0.0;
          if (mode == 1) {
            il += v; ilc++;
            if (ilc == 1 || v < ilmin) ilmin = v;
            if (ilc == 1 || v > ilmax) ilmax = v;
          } else if (mode == 2) {
            ip += v; ipc++;
            if (ipc == 1 || v < ipmin) ipmin = v;
            if (ipc == 1 || v > ipmax) ipmax = v;
          }
        }
      }
      if ($i ~ /^stage_ms\(last:/) stage_seen = 1;
    }
  }
  END {
    if (cc == 0) {
      print "No [lenses] debug telemetry lines found.";
      exit 1;
    }
    printf("telemetry_samples=%d\n", cc);
    printf("submit_fps_mean=%.3f submit_fps_min=%.3f submit_fps_max=%.3f\n", s/sc, smin, smax);
    printf("complete_fps_mean=%.3f complete_fps_min=%.3f complete_fps_max=%.3f\n", c/cc, cmin, cmax);
    printf("drop_fps_mean=%.3f drop_fps_min=%.3f drop_fps_max=%.3f\n", d/dc, dmin, dmax);
    if (ilc > 0)
      printf("infer_ms_last_mean=%.3f infer_ms_last_min=%.3f infer_ms_last_max=%.3f\n", il/ilc, ilmin, ilmax);
    if (ipc > 0) {
      p95_mean = ip/ipc;
      ceiling = p95_mean > 0 ? 1000.0 / p95_mean : 0.0;
      printf("infer_ms_p95_mean=%.3f infer_ms_p95_min=%.3f infer_ms_p95_max=%.3f infer_fps_ceiling_est=%.3f\n",
             p95_mean, ipmin, ipmax, ceiling);
    }
    printf("stage_metrics_present=%s\n", stage_seen ? "yes" : "unknown");
  }
' "$LOG_PATH"
