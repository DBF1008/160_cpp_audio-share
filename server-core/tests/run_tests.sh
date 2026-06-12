#!/usr/bin/env bash
#
# Build and run the server-core lifecycle regression tests WITHOUT cmake/vcpkg,
# using a compiler and the dependencies available on the current machine
# (here: macOS + Homebrew). On a normal CI/dev box with cmake + vcpkg, prefer:
#
#     cmake --preset <preset> -DAUDIO_SHARE_BUILD_TESTS=ON
#     cmake --build --preset <preset> --target server-core-tests
#     ctest --test-dir server-core/out/build/<preset>
#
# This script exists so the tests can be exercised on platforms that have no
# real audio backend: it compiles the real network_manager/audio_manager
# against the fake audio backend (AUDIO_SHARE_FAKE_AUDIO).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORE="$(cd "$HERE/.." && pwd)"        # server-core
SRC="$CORE/src"
PROTO_DIR="$(cd "$CORE/../protos" && pwd)"
BUILD="$CORE/out/test-host"
mkdir -p "$BUILD"

CXX="${CXX:-clang++}"

if command -v brew >/dev/null 2>&1; then
    ASIO_INC="$(brew --prefix asio)/include"
    SPDLOG_INC="$(brew --prefix spdlog)/include"
    FMT_INC="$(brew --prefix fmt)/include"
    export PKG_CONFIG_PATH="$(brew --prefix protobuf)/lib/pkgconfig:$(brew --prefix abseil)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
else
    # Best-effort defaults; override the *_INC vars in the environment if needed.
    ASIO_INC="${ASIO_INC:-/usr/include}"
    SPDLOG_INC="${SPDLOG_INC:-/usr/include}"
    FMT_INC="${FMT_INC:-/usr/include}"
fi

PROTO_CFLAGS="$(pkg-config --cflags protobuf)"
PROTO_LIBS="$(pkg-config --libs protobuf)"

echo "==> generating protobuf sources"
protoc --proto_path="$PROTO_DIR" --cpp_out="$BUILD" "$PROTO_DIR/client.proto"

echo "==> compiling server-core-tests"
# Header-only spdlog with external header-only fmt, matching the project's
# direct use of the fmt:: API (see src/formatter.hpp).
"$CXX" -std=c++20 -O1 -g -pthread -Wall -Wno-macro-redefined \
    -DAUDIO_SHARE_FAKE_AUDIO \
    -DSPDLOG_HEADER_ONLY -DSPDLOG_FMT_EXTERNAL -DFMT_HEADER_ONLY \
    -I"$SRC" -I"$HERE" -I"$BUILD" \
    -I"$ASIO_INC" -I"$SPDLOG_INC" -I"$FMT_INC" \
    $PROTO_CFLAGS \
    "$SRC/network_manager.cpp" \
    "$SRC/audio_manager.cpp" \
    "$HERE/fake_audio_manager_impl.cpp" \
    "$HERE/lifecycle_tests.cpp" \
    "$BUILD/client.pb.cc" \
    $PROTO_LIBS \
    -o "$BUILD/server-core-tests"

echo "==> running server-core-tests"
"$BUILD/server-core-tests"
