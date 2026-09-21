#!/usr/bin/env bash
# Block until every workflow named in WAIT_WORKFLOWS has a successful run for GITHUB_SHA.
#
# release.yml and mirror.yml publish from a push to main, in parallel with the test
# workflows, so without this gate a commit that breaks a test still reaches Maven Central
# and both public mirrors. The test workflows already run for the same commit; this waits
# on their results instead of repeating them.
set -euo pipefail
: "${GITHUB_REPOSITORY:?}" "${GITHUB_SHA:?}" "${WAIT_WORKFLOWS:?}"

deadline=$(( SECONDS + ${WAIT_TIMEOUT_SECONDS:-2400} ))
while :; do
  pending=""
  for workflow in $WAIT_WORKFLOWS; do
    line=$(gh api \
      "repos/$GITHUB_REPOSITORY/actions/workflows/$workflow/runs?head_sha=$GITHUB_SHA&per_page=1" \
      --jq '.workflow_runs[0] // {} | "\(.status // "missing") \(.conclusion // "")"' \
      2>/dev/null || echo "unknown ")
    status=${line%% *}
    conclusion=${line#* }
    case "$status $conclusion" in
      "completed success") ;;
      completed\ *) echo "::error::$workflow concluded $conclusion on $GITHUB_SHA"; exit 1 ;;
      *) pending="$pending $workflow($status)" ;;
    esac
  done
  if [ -z "$pending" ]; then
    echo "All required workflows succeeded on $GITHUB_SHA."
    exit 0
  fi
  if [ "$SECONDS" -ge "$deadline" ]; then
    echo "::error::timed out waiting for:$pending"
    exit 1
  fi
  echo "waiting for:$pending"
  sleep 15
done
