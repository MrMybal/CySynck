#!/usr/bin/env sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
desktop=${CYSYNCK_DESKTOP:-ON}
npm ci --prefix "$project_dir/.CyDocBuild"
cmake -S "$project_dir" -B "$project_dir/build" -DCMAKE_BUILD_TYPE=Release -DCYSYNCK_DESKTOP="$desktop"
cmake --build "$project_dir/build" --parallel
ctest --test-dir "$project_dir/build" --output-on-failure
cmake --install "$project_dir/build" --prefix "$project_dir/dist"
