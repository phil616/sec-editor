#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d)"
wine_command="${WINE_COMMAND:-wine}"
cleanup() {
  rm -rf "${test_dir}"
}
trap cleanup EXIT

printf 'token=秘密\r\n' > "${test_dir}/input.txt"
set +e
timeout 5s xvfb-run -a "${wine_command}" "${project_dir}/build-windows/mempad.exe" \
  "Z:${test_dir//\//\\}\\input.txt"
status=$?
set -e
if [[ ${status} -ne 124 ]]; then
  echo "mempad command-line startup failed with status ${status}" >&2
  exit 1
fi
if find "${test_dir}" -type f \( -name '*.tmp' -o -name '*.bak' -o -name '*.log' \) | grep -q .; then
  echo "unexpected auxiliary file created" >&2
  exit 1
fi
echo "Wine command-line startup test passed"
