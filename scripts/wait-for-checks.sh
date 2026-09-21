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
# A run that does not exist yet returns an empty result, not an API error. Repeated API
# errors mean the token or the workflow name is wrong, so fail rather than hang.
api_errors=0
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
      unknown\ *)
        api_errors=$(( api_errors + 1 ))
        if [ "$api_errors" -ge 8 ]; then
          echo "::error::cannot read workflow runs; check the token's actions:read permission and the workflow names."
          exit 1
        fi
        pending="$pending $workflow(unreadable)" ;;
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
