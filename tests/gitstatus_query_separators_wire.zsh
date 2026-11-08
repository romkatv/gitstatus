#!/usr/bin/env zsh
#
# Regression test for gitstatus_query input validation.
#
# gitstatusd's wire protocol uses 0x1F (field separator) and 0x1E (record separator). A directory
# name or -c callback containing either byte must be rejected by gitstatus_query before anything is
# written to the daemon: the query reports norepo-sync (directory) or fails with an error (-c), the
# in-flight counter stays at zero, and the request log stays unchanged.
#
# The real gitstatusd is run behind a small wrapper that records every byte written to its request
# pipe, so "no request was sent" is asserted on the actual bytes, not on plugin state alone.
#
# Usage: tests/gitstatus_query_separators_wire.zsh [-p PLUGIN_DIR] [-d GITSTATUSD]
#   -p PLUGIN_DIR  directory containing gitstatus.plugin.zsh, install and build.info
#                  (default: the parent of this script's directory)
#   -d GITSTATUSD  path to a real gitstatusd binary
#                  (default: ~/.cache/gitstatus/gitstatusd-<os>-<arch>)
#
# gitstatus_start needs job control, i.e. a controlling terminal. When running headless (CI, a
# tool harness), give it a pseudo-terminal:  script -q /dev/null zsh tests/gitstatus_query_separators_wire.zsh
#
# Exit status: 0 if every assertion passed, 1 otherwise.

# gitstatus.plugin.zsh refuses to load in a non-interactive shell; re-exec without rc files.
[[ -o interactive ]] || exec zsh -fi -- "$0" "$@"

emulate -L zsh -o extended_glob -o no_aliases -o typeset_silent
zmodload zsh/stat zsh/system

local script_dir=${${(%):-%x}:A:h}
local plugin_dir=${script_dir:h}
local daemon=
local opt OPTARG
local -i OPTIND
while getopts ":p:d:" opt; do
  case $opt in
    p) plugin_dir=${OPTARG:A};;
    d) daemon=${OPTARG:A};;
    *) print -ru2 -- "usage: $0 [-p PLUGIN_DIR] [-d GITSTATUSD]"; exit 2;;
  esac
done

if [[ -z $daemon ]]; then
  local uname_sm="${${(L)$(command uname -sm)}//ı/i}"
  daemon=${GITSTATUS_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/gitstatus}/gitstatusd-${uname_sm% *}-${uname_sm#* }
fi

[[ -r $plugin_dir/gitstatus.plugin.zsh ]] || { print -ru2 -- "no gitstatus.plugin.zsh in $plugin_dir"; exit 2 }
[[ -r $plugin_dir/install && -r $plugin_dir/build.info ]] || { print -ru2 -- "no install/build.info in $plugin_dir"; exit 2 }
[[ -x $daemon ]] || { print -ru2 -- "gitstatusd not executable: $daemon"; exit 2 }

local tmp
tmp=$(command mktemp -d "${TMPDIR:-/tmp}/gitstatus-sep-test.XXXXXX") || exit 2
local reqlog=$tmp/requests.log
: >$reqlog

# Wrapper: tee every request byte into $reqlog, then forward to the real daemon. The -G version
# glob is dropped so that a cached daemon of a different patch version is accepted.
local wrapper=$tmp/gitstatusd-wrapper
{
  print -r -- '#!/bin/sh'
  print -r -- "reqlog=${(qq)reqlog}"
  print -r -- "real=${(qq)daemon}"
  print -r -- 'n=$#; i=0'
  print -r -- 'while [ $i -lt $n ]; do'
  print -r -- '  a=$1; shift; i=$((i+1))'
  print -r -- '  if [ "$a" = -G ]; then shift; i=$((i+1)); continue; fi'
  print -r -- '  set -- "$@" "$a"'
  print -r -- 'done'
  print -r -- 'exec tee -a "$reqlog" | exec "$real" "$@"'
} >$wrapper
chmod +x $wrapper

# A small real repository to use as the positive control.
local repo=$tmp/repo
command git init -q $repo && command git -C $repo -c user.name=t -c user.email=t@t commit -q --allow-empty -m init || exit 2

local -i failures=0 checks=0
function check() {
  local label=$1 expected=$2 actual=$3
  (( ++checks ))
  if [[ $expected == $actual ]]; then
    print -r -- "  PASS  $label"
  else
    print -r -- "  FAIL  $label: expected ${(qqq)expected}, got ${(qqq)actual}"
    (( ++failures ))
  fi
}

function reqlog_size() {
  command sleep 0.3  # let tee flush anything the plugin wrote
  local -a st
  zstat -A st +size -- $reqlog
  print -r -- $st[1]
}

function section() { print; print -r -- "$1"; }

export GITSTATUS_DAEMON=$wrapper GITSTATUS_AUTO_INSTALL=0 GITSTATUS_CACHE_DIR=$tmp/cache
unset GIT_DIR
builtin set --  # the plugin uses $1 as a function-name suffix; it must be empty
builtin source $plugin_dir/gitstatus.plugin.zsh || exit 2

print -r -- "plugin:     $plugin_dir/gitstatus.plugin.zsh"
print -r -- "gitstatusd: $daemon ($($daemon --version 2>&1))"
print -r -- "zsh:        $ZSH_VERSION"

if ! gitstatus_start -t 10 TEST; then
  print -ru2 -- "gitstatus_start failed"
  command rm -rf -- $tmp
  exit 2
fi

section "positive control: query a real repository"
local -i before=$(reqlog_size)
gitstatus_query -t 10 -d $repo TEST; local -i ret=$?
check "gitstatus_query returns 0" 0 $ret
check "VCS_STATUS_RESULT is ok-sync" ok-sync "$VCS_STATUS_RESULT"
check "a request was recorded" 1 $(( $(reqlog_size) > before ))

local -a bad_bytes=($'\x1e' $'\x1f')
local byte name
for byte in $bad_bytes; do
  name=$(printf '0x%02X' "'$byte")

  section "-d directory containing $name"
  before=$(reqlog_size)
  gitstatus_query -t 5 -d "$repo/sub${byte}dir" TEST; ret=$?
  check "gitstatus_query returns 0" 0 $ret
  check "VCS_STATUS_RESULT is norepo-sync" norepo-sync "$VCS_STATUS_RESULT"
  check "no request was sent" $before $(reqlog_size)
  check "no request left in flight" 0 $(( _GITSTATUS_NUM_INFLIGHT_TEST ))

  section "\$PWD containing $name"
  local cwd=$repo/pwd${byte}dir
  if command mkdir -- $cwd 2>/dev/null && builtin cd -q -- $cwd; then
    before=$(reqlog_size)
    gitstatus_query -t 5 TEST; ret=$?
    builtin cd -q -- $tmp
    check "gitstatus_query returns 0" 0 $ret
    check "VCS_STATUS_RESULT is norepo-sync" norepo-sync "$VCS_STATUS_RESULT"
    check "no request was sent" $before $(reqlog_size)
    check "no request left in flight" 0 $(( _GITSTATUS_NUM_INFLIGHT_TEST ))
  else
    print -r -- "  SKIP  filesystem refuses directory names containing $name"
  fi

  section "-c callback containing $name"
  before=$(reqlog_size)
  gitstatus_query -t 5 -c "x${byte}y" -d $repo TEST 2>/dev/null; ret=$?
  check "gitstatus_query returns 1" 1 $ret
  check "no request was sent" $before $(reqlog_size)
  check "no request left in flight" 0 $(( _GITSTATUS_NUM_INFLIGHT_TEST ))
done

section "positive control: the daemon still answers"
before=$(reqlog_size)
gitstatus_query -t 10 -d $repo TEST; ret=$?
check "gitstatus_query returns 0" 0 $ret
check "VCS_STATUS_RESULT is ok-sync" ok-sync "$VCS_STATUS_RESULT"
check "a request was recorded" 1 $(( $(reqlog_size) > before ))

gitstatus_stop TEST
builtin cd -q /
command rm -rf -- $tmp

print
print -r -- "$checks checks, $failures failed"
(( failures == 0 ))
