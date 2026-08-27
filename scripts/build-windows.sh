#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build-windows"
delivery_dir="${project_dir}/dist"
export SOURCE_DATE_EPOCH=0

strip_command="x86_64-w64-mingw32-strip"
objdump_command="x86_64-w64-mingw32-objdump"
if ! command -v "${strip_command}" >/dev/null 2>&1; then
  strip_command="strip"
fi
if ! command -v "${objdump_command}" >/dev/null 2>&1; then
  objdump_command="objdump"
fi

rm -rf "${build_dir}"
cmake \
  -S "${project_dir}" \
  -B "${build_dir}" \
  -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="${project_dir}/cmake/toolchain-mingw64.cmake" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "${build_dir}"
"${strip_command}" "${build_dir}/mempad.exe"
file "${build_dir}/mempad.exe"
"${objdump_command}" -p "${build_dir}/mempad.exe" > "${build_dir}/pe-info.txt"

rm -rf "${delivery_dir}"
cmake -E make_directory "${delivery_dir}"
cmake -E copy "${build_dir}/mempad.exe" "${delivery_dir}/mempad.exe"
cmake -E copy "${project_dir}/README.md" "${delivery_dir}/README.md"
cmake -E copy "${project_dir}/SECURITY.md" "${delivery_dir}/SECURITY.md"
