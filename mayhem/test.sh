#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the known-answer oracle (ttable_unit_test) that mayhem/build.sh produced.
# It asserts concrete computed VALUES of noxim's traffic-table routing/region helpers — the exact
# code the fuzz targets exercise — so a no-op / exit(0) PATCH fails it (anti-reward-hacking).
# Emits a CTRF (https://ctrf.io) summary and exits non-zero iff a test failed.
#
# NB: no `set -e` — a grep that finds no match returns non-zero, which under pipefail would abort
# the script before we emit the CTRF report.
set -uo pipefail
: "${SRC:=/mayhem}"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

UTEST="$SRC/ttable_unit_test"
[ -x "$UTEST" ] || { echo "FATAL: $UTEST missing — mayhem/build.sh did not build the test oracle" >&2; emit_ctrf "noxim-ttable-kat" 0 1 0; exit 1; }

LOG=/tmp/ttable_unit_test.log
"$UTEST" 2>&1 | tee "$LOG"

# The runner prints a final: TESTS total=N passed=P failed=F
summary=$(grep -oE 'TESTS total=[0-9]+ passed=[0-9]+ failed=[0-9]+' "$LOG" | tail -1 || true)
passed=$(echo "$summary" | grep -oE 'passed=[0-9]+' | grep -oE '[0-9]+' || true)
failed=$(echo "$summary" | grep -oE 'failed=[0-9]+' | grep -oE '[0-9]+' || true)
: "${passed:=0}" "${failed:=0}"

# If the runner printed no summary line, it crashed/aborted — treat as a hard failure.
if [ -z "$summary" ]; then
  echo "FATAL: ttable_unit_test did not complete (crash/abort?)" >&2
  emit_ctrf "noxim-ttable-kat" "$passed" "$(( failed > 0 ? failed : 1 ))"
  exit 1
fi

emit_ctrf "noxim-ttable-kat" "$passed" "$failed"
