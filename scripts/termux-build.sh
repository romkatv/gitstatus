#!/usr/bin/env bash
set -euo pipefail

# Termux automation script for building libgit2 and gitstatusd
# Usage: ./scripts/termux-build.sh [REPO_DIR] [INSTALL_PREFIX]
# Defaults: REPO_DIR=$PWD, INSTALL_PREFIX=$HOME/local

REPO_DIR="${1:-$PWD}"
PREFIX="${2:-$HOME/local}"

echo "Repo: $REPO_DIR"
echo "Install prefix: $PREFIX"

echo "Installing Termux packages..."
pkg update -y
pkg install -y git clang cmake make autoconf automake libtool pkg-config \
  bash curl tar unzip coreutils openssl zlib

mkdir -p "$PREFIX"

# Build patched libgit2 (romkatv/libgit2)
LG2_SRC="$HOME/libgit2"
if [ ! -d "$LG2_SRC" ]; then
  echo "Cloning romkatv/libgit2 into $LG2_SRC"
  git clone https://github.com/romkatv/libgit2.git "$LG2_SRC"
fi

echo "Building libgit2..."
mkdir -p "$LG2_SRC/build"
cd "$LG2_SRC/build"
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=OFF \
      -DBUILD_CLAR=OFF \
      -DUSE_SSH=OFF \
      -DCMAKE_INSTALL_PREFIX="$PREFIX" ..
make -j"$(nproc)"
make install

export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"

# Build gitstatusd
echo "Building gitstatusd in $REPO_DIR"
cd "$REPO_DIR"

# Extract version from build.info
version=
if [ -f build.info ]; then
  . ./build.info
  version="$gitstatus_version"
fi

make clean || true
make CXX=clang++ \
     CXXFLAGS="-I$PREFIX/include${version:+ -DGITSTATUS_VERSION=$version}" \
     LDFLAGS="-L$PREFIX/lib" \
     LDLIBS="-lgit2 -lcrypto -lssl -lz -lunwind" \
     -j"$(nproc)"

install -d "$PREFIX/bin"
install -m 755 "$REPO_DIR/usrbin/gitstatusd" "$PREFIX/bin/gitstatusd"

echo
echo "Build finished. If successful, the binary is at: $REPO_DIR/usrbin/gitstatusd"
echo "Installed copy to: $PREFIX/bin/gitstatusd"
echo "To run from Termux (example):"
echo "  export PATH=\"$PREFIX/bin:\$PATH\""
echo "  export GITSTATUS_DAEMON=\"$PREFIX/bin/gitstatusd\""
echo "  bash -ic 'cd \"$REPO_DIR\" && source ./gitstatus.plugin.sh && gitstatus_start -t 5'"

exit 0