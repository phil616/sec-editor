#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${project_dir}/build-linux"

cmake -S "${project_dir}" -B "${build_dir}" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "${build_dir}"
ctest --test-dir "${build_dir}" --output-on-failure
