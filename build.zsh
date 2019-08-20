#!/bin/zsh
#
# How to build:
#
#   zsh -c "$(curl -fsSL https://raw.githubusercontent.com/romkatv/gitstatus/master/build.zsh)"
#
# If everything goes well, the path to your newly built binary will be printed at the end.
#
# If something breaks due to a missing dependency (e.g., `cmake` not found), install the
# dependency, remove `${TMPDIR:-/tmp}/gitstatus` and retry.

readonly GITSTATUS_REPO_URL=https://github.com/romkatv/gitstatus.git
readonly LIBGIT2_REPO_URL=https://github.com/romkatv/libgit2.git

emulate -L zsh
setopt err_return err_return no_unset pipe_fail extended_glob typeset_silent

[[ $# -lt 2 ]] || {
  echo "Usage: build.sh [DIR]" >&2
  return 1
}

local DIR=${${1:-${TMPDIR:-/tmp}/gitstatus}:a}
local OS && OS=$(uname -s)
[[ $OS != Linux || $(uname -o) != Android ]] || OS=Android

local CPUS
case $OS in
  FreeBSD) CPUS=$(sysctl -n hw.ncpu);;
  *) CPUS=$(getconf _NPROCESSORS_ONLN);;
esac

function build_iconv() {
  [[ $OS == Darwin ]] || return 0
  cd $DIR
  [[ -n libiconv(#qFN) ]] || {
    rm -rf iconv libiconv-51.200.6.tar.gz libiconv-51.200.6
    curl -fsSLO https://opensource.apple.com/tarballs/libiconv/libiconv-51.200.6.tar.gz
    tar xvzf libiconv-51.200.6.tar.gz
    mv libiconv-51.200.6/libiconv .
  }
  cd libiconv
  ./configure --enable-static
  make -j $CPUS
  cp lib/.libs/libiconv.a .
}

function build_libgit2() {
  cd $DIR
  [[ -n libgit2(#qFN) ]] || git clone --depth 1 $LIBGIT2_REPO_URL
  mkdir -p libgit2/build
  cd libgit2/build
  local -a cmakeflags=(${(@Q)${(z)CMAKEFLAGS:-}})
  case $OS in
    Darwin)
      cmakeflags+=-DUSE_ICONV=ON
      ;;
  esac
  cmake                        \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHREADSAFE=ON            \
    -DUSE_BUNDLED_ZLIB=ON      \
    -DREGEX_BACKEND=builtin    \
    -DBUILD_CLAR=OFF           \
    -DUSE_SSH=OFF              \
    -DUSE_HTTPS=OFF            \
    -DBUILD_SHARED_LIBS=OFF    \
    -DUSE_EXT_HTTP_PARSER=OFF  \
    -DZERO_NSEC=ON             \
    ${(@Q)${(z)CMAKEFLAGS:-}}  \
    ..
  make -j $CPUS
}

function build_gitstatus() {
  cd $DIR
  [[ -n gitstatus(#qFN) ]] || git clone --depth 1 $GITSTATUS_REPO_URL
  cd gitstatus
  local cxx=${CXX:-'g++'}
  local cxxflags=${CXXFLAGS:-''}
  local ldflags=${LDFLAGS:-''}
  local make=make
  cxxflags+=" -I$DIR/libgit2/include -DGITSTATUS_ZERO_NSEC"
  ldflags+=" -L$DIR/libgit2/build"
  case $OS in
    Android)
      cxx=${CXX:-'clang++'}
      ;;
    Linux)
      ldflags+=" -static-libstdc++ -static-libgcc"
      ;;
    FreeBSD)
      ldflags+=" -static"
      make=gmake
      ;;
    Darwin)
      cxxflags+=" -I$DIR/libiconv/include"
      ldflags+=" -L$DIR/libiconv -liconv"
      ;;
    CYGWIN*)
      cxxflags+=" -D_GNU_SOURCE"
      ldflags+=" -static"
      ;;
  esac
  CXX=$cxx CXXFLAGS=$cxxflags LDFLAGS=$ldflags $make -j $CPUS
  strip gitstatusd
  local arch && arch=$(uname -m)
  local target=$PWD/bin/gitstatusd-${OS:l}-${arch:l}
  cp -f gitstatusd $target
  echo "built: $target" >&2
}

function verify_gitstatus() {
  local reply
  echo -nE $'hello\x1f\x1e' | $DIR/gitstatus/gitstatusd 2>/dev/null | {
    IFS='' read -r -d $'\x1e' -t 5 reply
    [[ $reply == $'hello\x1f0' ]]
  }
  echo "self-check successful" >&2
}

echo "Building gitstatus in $DIR ..." >&2
mkdir -p $DIR
build_iconv
build_libgit2
build_gitstatus
verify_gitstatus
