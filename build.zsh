#!/usr/bin/env zsh
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
readonly ICONV_TARBALL_URL=https://opensource.apple.com/tarballs/libiconv/libiconv-51.200.6.tar.gz

emulate -L zsh
setopt pipe_fail no_aliases extended_glob typeset_silent

if [[ $# > 1 ]]; then
  print -ru2 -- "usage: build.zsh [dir]"
  return 1
fi

local dir=${${1:-${TMPDIR:-/tmp}/gitstatus}:a}

local kernel
kernel="${(L)$(uname -s)}" || return
[[ -n $kernel ]]           || return

local cpus
if (( ! $+commands[sysctl] )) || [[ $kernel == linux ]] || ! cpus="$(sysctl -n hw.ncpu)"; then
  if (( ! $+commands[getconf] )) || ! cpus="$(getconf _NPROCESSORS_ONLN)"; then
    cpus=8
  fi
fi
[[ $cpus == <1-> ]] || cpus=8

function build_iconv() {
  [[ $kernel == darwin ]]             || return 0
  cd -- $dir                          || return

  local tarball=${ICONV_TARBALL_URL:t}
  local base=${tarball%.tar.gz}

  if [[ -z libiconv(#qFN) ]]; then
    rm -rf -- iconv $tarball $base    || return
    curl -fsSLO -- $ICONV_TARBALL_URL || return
    tar xvzf $tarball                 || return
    mv $base/libiconv .               || return
  fi
  cd libiconv                         || return
  ./configure --enable-static         || return
  make -j $cpus                       || return
  cp lib/.libs/libiconv.a .           || return
}

function build_libgit2() {
  cd $dir                                 || return
  if [[ -z libgit2(#qFN) ]]; then
    git clone --depth 1 $LIBGIT2_REPO_URL || return
  fi
  mkdir -p libgit2/build                  || return
  cd libgit2/build                        || return
  local -a cmakeflags=(${(@Q)${(z)CMAKEFLAGS}})
  [[ $kernel == darwin ]] && cmakeflags+=-DUSE_ICONV=ON
  cmake                        \
    -DCMAKE_BUILD_TYPE=Release \
    -DTHREADSAFE=ON            \
    -DUSE_BUNDLED_ZLIB=ON      \
    -DREGEX_BACKEND=builtin    \
    -DBUILD_CLAR=OFF           \
    -DUSE_SSH=OFF              \
    -DUSE_HTTPS=OFF            \
    -DBUILD_SHARED_LIBS=OFF    \
    -DZERO_NSEC=ON             \
    $cmakeflags                \
    ..                                    || return
  make -j $cpus                           || return
}

function build_gitstatus() {
  cd $dir                                   || return
  if [[ -z gitstatus(#qFN) ]]; then
    git clone --depth 1 $GITSTATUS_REPO_URL || return
  fi
  cd gitstatus                              || return

  local os=$kernel
  if [[ $os == linux ]]; then
    os="${(L)$(uname -o 2>/dev/null)}"      || os=
    [[ $os == android ]]                    || os=linux
  fi

  local arch
  arch="${(L)$(uname -m)}"                  || return
  [[ -n $arch ]]                            || return

  local cxx=${CXX:-'g++'}
  local cxxflags=${CXXFLAGS}
  local ldflags=${LDFLAGS}
  local make=make

  cxxflags+=" -I${(q)dir}/libgit2/include -DGITSTATUS_ZERO_NSEC"
  ldflags+=" -L${(q)dir}/libgit2/build"

  case $os in
    android)
      cxx=${CXX:-'clang++'}
      [[ $arch != 'armv7l' ]] || ldflags+=" -latomic"
      ;;
    linux)
      ldflags+=" -static-libstdc++ -static-libgcc"
      ;;
    freebsd)
      ldflags+=" -static"
      make=gmake
      ;;
    openbsd)
      cxx=${CXX:-'eg++'}
      ldflags+=" -static"
      make=gmake
      ;;
    darwin)
      cxxflags+=" -I${(q)dir}/libiconv/include"
      ldflags+=" -L${(q)dir}/libiconv -liconv"
      ;;
    cygwin*|msys*)
      cxxflags+=" -D_GNU_SOURCE"
      ldflags+=" -static"
      ;;
  esac

  rm -rf usrbin                                               || return
  CXX=$cxx CXXFLAGS=$cxxflags LDFLAGS=$ldflags $make -j $cpus || return
  strip usrbin/gitstatusd                                     || return
  mv -f -- usrbin/gitstatusd{,-$os-$arch} $target             || return
}

function verify_gitstatus() {
  local reply
  print -rn -- $'hello\x1f\x1e' | $dir/gitstatus/usrbin/* 2>/dev/null | {
    IFS='' read -r -d $'\x1e' -t 5 reply || return
    [[ $reply == $'hello\x1f0' ]]        || return
  } || return
  print -ru2 -- "self-check successful"
}

print -ru2 -- "Building gitstatus in $dir ..."
mkdir -p $dir     || return
build_iconv       || return
build_libgit2     || return
build_gitstatus   || return
verify_gitstatus  || return

print -ru2 -- built: $dir/gitstatus/usrbin/* >&2
