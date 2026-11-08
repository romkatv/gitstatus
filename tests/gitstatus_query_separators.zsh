#!/usr/bin/env zsh
#
# Regression test for gitstatus_query: a directory name that contains one of the bytes gitstatusd
# uses as record (0x1E) and field (0x1F) separators must never be written into a request, and
# the queries that follow must still be paired with their own replies.
#
# Usage: zsh tests/gitstatus_query_separators.zsh
#
# Requires git, a terminal (gitstatus_start needs job control; without a terminal run the test
# under script(1)) and a gitstatusd binary. The binary is located the way gitstatus_start locates
# it (GITSTATUS_DAEMON, usrbin/, then the gitstatus cache directory) but is never downloaded.
#
# Exits with 0 if every check passes and with 1 otherwise.

# gitstatus.plugin.zsh loads only in interactive shells, so run again in one that reads no rc files.
[[ -o interactive ]] || exec zsh -fi -- "$0" "$@"

emulate -R zsh
setopt extended_glob

zmodload zsh/system || exit 1

typeset -gi checks=0 failures=0

function ok() {
  (( ++checks ))
  print -r -- "ok   $1"
}

function fail() {
  (( ++checks, ++failures ))
  print -ru2 -- "FAIL $1"
}

# check DESCRIPTION EXPECTED ACTUAL
function check() {
  if [[ $3 == "$2" ]]; then
    ok "$1"
  else
    fail "$1: expected '${(V)2}', got '${(V)3}'"
  fi
}

# check_match DESCRIPTION PATTERN ACTUAL
function check_match() {
  if [[ $3 == ${~2} ]]; then
    ok "$1"
  else
    fail "$1: expected a match for '${(V)2}', got '${(V)3}'"
  fi
}

# The number of requests gitstatusd has logged so far.
function requests_seen() {
  local -i n
  n=$(command grep -c 'Processing request: ' -- $GITSTATUS_DAEMON_LOG_TEST 2>/dev/null)
  print -r -- $n
}

# expect_rejected DESCRIPTION [GITSTATUS_QUERY_OPTION]...
#
# Runs gitstatus_query with the given options and verifies that the directory was reported as not
# a repository without anything being sent to gitstatusd.
function expect_rejected() {
  local what=$1
  shift
  local -i before=$(requests_seen) ret
  gitstatus_query -t 10 "$@" TEST
  ret=$?
  check "$what: return status"              0           $ret
  check "$what: VCS_STATUS_RESULT"          norepo-sync "${VCS_STATUS_RESULT-}"
  check "$what: VCS_STATUS_WORKDIR is unset" 0          $+VCS_STATUS_WORKDIR
  check "$what: no requests in flight"      0           $_GITSTATUS_NUM_INFLIGHT_TEST
  check "$what: nothing sent to gitstatusd" $before     $(requests_seen)
}

tmp=$(command mktemp -d "${TMPDIR:-/tmp}/gitstatus-test.XXXXXX") || exit 1
tmp=${tmp:A}

function cleanup() {
  (( $+functions[gitstatus_stop] )) && gitstatus_stop TEST 2>/dev/null
  command rm -rf -- $tmp
}
trap cleanup EXIT

# gitstatus_start keeps its lock file and logs in TMPDIR.
export TMPDIR=$tmp

repo=$tmp/repo
command git init -q $repo || exit 1
command git -C $repo -c user.name=test -c user.email=test@example.com \
  commit -q --allow-empty -m init || exit 1
weird=$repo/sub$'\x1e'dir  # a directory in the repository whose name contains 0x1E
command mkdir -p -- $weird || exit 1
plain=$tmp/plain           # not a repository
command mkdir -p -- $plain || exit 1

source ${0:A:h}/../gitstatus.plugin.zsh || exit 1

export GITSTATUS_AUTO_INSTALL=0 GITSTATUS_LOG_LEVEL=INFO
if ! gitstatus_start TEST; then
  print -ru2 -- "FAIL gitstatus_start TEST (is there a gitstatusd binary?)"
  exit 1
fi

typeset -i ret

# A regular query works.
gitstatus_query -t 10 -d $repo TEST
ret=$?
check "baseline: return status"         0       $ret
check "baseline: VCS_STATUS_RESULT"     ok-sync "${VCS_STATUS_RESULT-}"
check "baseline: VCS_STATUS_WORKDIR"    $repo   "${VCS_STATUS_WORKDIR:A}"
check "baseline: no requests in flight" 0       $_GITSTATUS_NUM_INFLIGHT_TEST

# Directory names with a separator byte are rejected before anything is sent.
expect_rejected "0x1E in -d" -d $repo/a$'\x1e'b
expect_rejected "0x1F in -d" -d $repo/a$'\x1f'b

builtin cd -q -- $weird || exit 1
expect_rejected "0x1E in the current directory"
builtin cd -q -- $tmp || exit 1

GIT_DIR=$repo/.git$'\x1f'x
expect_rejected "0x1F in GIT_DIR"
unset GIT_DIR

# A callback with a separator byte is rejected with an error.
() {
  local -i before=$(requests_seen)
  gitstatus_query -t 10 -c $'callback\x1f' -d $repo TEST 2>$tmp/stderr
  ret=$?
  check       "-c with 0x1F: return status"            1 $ret
  check_match "-c with 0x1F: error message"            'gitstatus_query: invalid -c argument: *' "$(<$tmp/stderr)"
  check       "-c with 0x1F: VCS_STATUS_RESULT is unset" 0 $+VCS_STATUS_RESULT
  check       "-c with 0x1F: no requests in flight"    0 $_GITSTATUS_NUM_INFLIGHT_TEST
  check       "-c with 0x1F: nothing sent to gitstatusd" $before $(requests_seen)
}

# The queries that follow are paired with their own replies.
gitstatus_query -t 10 -d $plain TEST
ret=$?
check "sync pairing: plain directory return status"     0           $ret
check "sync pairing: plain directory VCS_STATUS_RESULT" norepo-sync "${VCS_STATUS_RESULT-}"
gitstatus_query -t 10 -d $repo TEST
ret=$?
check "sync pairing: repository return status"          0           $ret
check "sync pairing: repository VCS_STATUS_RESULT"      ok-sync     "${VCS_STATUS_RESULT-}"
check "sync pairing: repository VCS_STATUS_WORKDIR"     $repo       "${VCS_STATUS_WORKDIR:A}"
check "sync pairing: no requests in flight"             0           $_GITSTATUS_NUM_INFLIGHT_TEST

typeset -gi callbacks=0
typeset -g callback_result= callback_workdir=
function on_result() {
  (( ++callbacks ))
  callback_result=${VCS_STATUS_RESULT-}
  callback_workdir=${VCS_STATUS_WORKDIR-}
}
# gitstatus_process_results returns only once no request is in flight, so it would never return
# if a request had been lost above.
if (( ret == 0 && _GITSTATUS_NUM_INFLIGHT_TEST == 0 )); then
  gitstatus_query -t 0 -c on_result -d $repo TEST
  ret=$?
  check "async pairing: return status"       0    $ret
  check "async pairing: VCS_STATUS_RESULT"   tout "${VCS_STATUS_RESULT-}"
  gitstatus_process_results -t 10 TEST
  ret=$?
  check "async pairing: gitstatus_process_results" 0        $ret
  check "async pairing: callback invoked once"     1        $callbacks
  check "async pairing: callback result"           ok-async $callback_result
  check "async pairing: callback workdir"          $repo    "${callback_workdir:A}"
  check "async pairing: no requests in flight"     0        $_GITSTATUS_NUM_INFLIGHT_TEST
else
  fail "async pairing: skipped, $_GITSTATUS_NUM_INFLIGHT_TEST request(s) in flight"
fi

# No unread reply is left behind.
leftover=
sysread -t 0.5 -i $_GITSTATUS_RESP_FD_TEST leftover
ret=$?
check "response pipe is drained" 4 $ret
[[ -z $leftover ]] || fail "unread reply: '${(V)leftover}'"

gitstatus_stop TEST || fail "gitstatus_stop TEST"

if (( failures )); then
  print -ru2 -- "$failures of $checks checks failed"
  exit 1
fi
print -r -- "all $checks checks passed"
