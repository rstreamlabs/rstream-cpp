#!/usr/bin/env bash

set -euo pipefail

[[ "$CANDIDATE_RUN_ID" =~ ^[1-9][0-9]*$ ]]
case "$RSTREAM_URL" in
  https://rstream.io|https://dev.rstream.io) ;;
  *) printf 'Unexpected package registry.\n' >&2; exit 1 ;;
esac
[[ "$CHANNEL" == stable || "$CHANNEL" == dev ]]
run=$(gh api "repos/$GITHUB_REPOSITORY/actions/runs/$CANDIDATE_RUN_ID")
jq -e --arg repo "$GITHUB_REPOSITORY" --arg actor "$ALLOWED_ACTOR" '
  .repository.full_name == $repo and .head_repository.full_name == $repo and
  .path == ".github/workflows/release-packages.yml" and .actor.login == $actor and
  .triggering_actor.login == $actor and (.head_sha | test("^[a-f0-9]{40}$"))
' <<< "$run" >/dev/null
candidate_sha=$(jq -r '.head_sha' <<< "$run")
event=$(jq -r '.event' <<< "$run")
ref=$(jq -r '.head_branch' <<< "$run")
tag=''
if [[ "$CANDIDATE_RUN_ID" != "$GITHUB_RUN_ID" ]]; then
  [[ "$GITHUB_REF" == refs/heads/main && "$event" == push ]]
fi
if [[ "$event" == push || "$GITHUB_REF_TYPE" == tag ]]; then
  [[ "$ref" =~ ^v?[0-9]+\.[0-9]+\.[0-9]+([+-][0-9A-Za-z.-]+)?$ ]]
  record=$(gh api "repos/$GITHUB_REPOSITORY/git/ref/tags/$ref" --jq .object)
  if [[ $(jq -r .type <<< "$record") == tag ]]; then
    object_sha=$(jq -er '.sha | select(test("^[0-9a-f]{40}$"))' <<< "$record")
    record=$(gh api "repos/$GITHUB_REPOSITORY/git/tags/$object_sha" --jq .object)
  fi
  jq -e --arg sha "$candidate_sha" '.type == "commit" and .sha == $sha' <<< "$record" >/dev/null
  tag=$ref
else
  [[ "$event" == workflow_dispatch && "$CANDIDATE_RUN_ID" == "$GITHUB_RUN_ID" && "$candidate_sha" == "$GITHUB_SHA" ]]
fi
printf 'sha=%s\ntag=%s\n' "$candidate_sha" "$tag" >> "$GITHUB_OUTPUT"
