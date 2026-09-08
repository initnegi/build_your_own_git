#!/bin/sh
#
# Build and run this Git implementation locally.
# This script is for local use only; it does not change CodeCrafters' build.
#
# Requirements:
#   - g++ in PATH (MSYS2 UCRT64 g++ works on Windows)
#   - zlib available to the compiler (MSYS2 UCRT64 includes it)
#
# Run from Git Bash, or from PowerShell with `bash your_program.sh <command>`:
#   ./your_program.sh init
#   ./your_program.sh clone https://github.com/octocat/Hello-World.git hello-world
#
# Run commands in a temporary directory so this program does not modify this
# repository's own .git directory. For example, from Git Bash:
#   mkdir -p /tmp/mini-git-test && cd /tmp/mini-git-test
#   /path/to/codecrafters-git-cpp/your_program.sh init
#
set -e

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BUILD_DIR="$SCRIPT_DIR/build"
PROGRAM="$BUILD_DIR/git.exe"
CXX=${CXX:-g++}

if ! command -v "$CXX" >/dev/null 2>&1; then
  for candidate in /ucrt64/bin/g++.exe /c/msys64/ucrt64/bin/g++.exe; do
    if [ -x "$candidate" ]; then
      CXX="$candidate"
      break
    fi
  done
fi

if ! command -v "$CXX" >/dev/null 2>&1 && [ ! -x "$CXX" ]; then
  echo "Could not find $CXX. Install g++ or set CXX to its path." >&2
  exit 1
fi

mkdir -p "$BUILD_DIR"

SOURCE_FILE="$SCRIPT_DIR/src/main.cpp"

case "$(uname -s 2>/dev/null || true)" in
  MINGW*)
    # Select the MSYS2 UCRT environment when this is launched from Git Bash.
    CXX_PATH=$(command -v "$CXX" 2>/dev/null || printf '%s' "$CXX")
    MSYSTEM=UCRT64 \
    MSYS2_PATH_TYPE=inherit \
    PATH="/c/msys64/ucrt64/bin:/c/msys64/usr/bin:$PATH" \
    "$CXX_PATH" \
      -std=c++23 \
      -Wall \
      -Wextra \
      -Wpedantic \
      "$SOURCE_FILE" \
      -lz \
      -o "$PROGRAM"
    ;;
  *)
    "$CXX" \
      -std=c++23 \
      -Wall \
      -Wextra \
      -Wpedantic \
      "$SOURCE_FILE" \
      -lz \
      -o "$PROGRAM"
    ;;
esac

case "$(uname -s 2>/dev/null || true)" in
  MINGW*)
    exec env \
      MSYSTEM=UCRT64 \
      MSYS2_PATH_TYPE=inherit \
      PATH="/c/msys64/ucrt64/bin:/mingw64/bin:/c/msys64/usr/bin:$PATH" \
      "$PROGRAM" "$@"
    ;;
  *)
    exec "$PROGRAM" "$@"
    ;;
esac
