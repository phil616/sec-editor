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
timeout 30s xvfb-run -a "${wine_command}" "${project_dir}/build-windows/安全编辑器.exe" \
  --smoke-test "Z:${test_dir//\//\\}\\input.txt" "Z:${test_dir//\//\\}\\output.txt"
printf 'token=秘密\r\n\r\nSafeEditor smoke' > "${test_dir}/expected.txt"
cmp "${test_dir}/expected.txt" "${test_dir}/output.txt"
if find "${test_dir}" -type f \( -name '*.tmp' -o -name '*.bak' -o -name '*.log' \) | grep -q .; then
  echo "unexpected auxiliary file created" >&2
  exit 1
fi
echo "Wine smoke test passed"
