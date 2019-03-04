# Return an error from gitstatus_query_dir after this many seconds.
: ${GITSTATUS_TIMEOUT_SEC=5}

# Path to gitstatusd. Defaults to gitstatusd in the same directory as this file.
: ${GITSTATUS_DAEMON=${${(%):-%x}:A:h}/gitstatusd}

# Report -1 unstaged and untracked if there are more than this many files in the index; negative
# value means infinity.
: ${GITSTATUS_DIRTY_MAX_INDEX_SIZE=-1}

# Retrives status of a git repo from a directory under its working tree.
#
#   $1 -- Directory to query. Defaults to $PWD. Must be absolute.
#
# If the directory is not in a git repo, returns an error. Otherwise returns success and sets
# the following global variables.
#
#   VCS_STATUS_LOCAL_BRANCH    Local branch name. Not empty.
#   VCS_STATUS_REMOTE_BRANCH   Upstream branch name. Can be empty.
#   VCS_STATUS_REMOTE_URL      Remote URL. Can be empty.
#   VCS_STATUS_ACTION          Repository state, A.K.A. action. Can be empty.
#   VCS_STATUS_HAS_STAGED      1 if there are staged changes, 0 otherwise.
#   VCS_STATUS_HAS_UNSTAGED    1 if there are unstaged changes, 0 if there aren't, -1 if unknown.
#   VCS_STATUS_HAS_UNTRACKED   1 if there are untracked files, 0 if there aren't, -1 if unknown.
#   VCS_STATUS_COMMITS_AHEAD   Number of commits the current branch is ahead of upstream.
#                              Non-negative integer.
#   VCS_STATUS_COMMITS_BEHIND  Number of commits the current branch is behind upstream. Non-negative
#                              integer.
#   VCS_STATUS_STASHES         Number of stashes. Non-negative integer.
#   VCS_STATUS_ALL             All of the above in an array. The order of elements is unspecified.
#                              More elements can be added in the future.
#
# The point of reporting -1 as unstaged and untracked is to allow the command to skip scanning
# files in large repos. See GITSTATUS_DIRTY_MAX_INDEX_SIZE above.
function gitstatus_query_dir() {
  emulate -L zsh
  setopt err_return no_unset

  [[ -v GITSTATUS_DAEMON_PID ]]

  if [[ $GITSTATUS_CLIENT_PID != $$ ]]; then
    exec {_GITSTATUS_REQ_FD}<>$_GITSTATUS_REQ_FIFO
    exec {_GITSTATUS_RESP_FD}<>$_GITSTATUS_RESP_FIFO
    GITSTATUS_CLIENT_PID=$$
  fi

  local ID=$EPOCHREALTIME
  echo -nE "${ID}"$'\x1f'"${1-"${PWD}"}"$'\x1e' >&$_GITSTATUS_REQ_FD

  while true; do
    typeset -g VCS_STATUS_ALL
    IFS=$'\x1f' \
      read -rd $'\x1e' -u $_GITSTATUS_RESP_FD -t $GITSTATUS_TIMEOUT_SEC -A VCS_STATUS_ALL || {
        echo "gitstatus: timed out" >&2
        return 1
      }
    [[ ${VCS_STATUS_ALL[1]} == $ID ]] || continue
    [[ ${VCS_STATUS_ALL[2]} == 1 ]]

    shift 2 VCS_STATUS_ALL
    typeset -g  VCS_STATUS_LOCAL_BRANCH="${VCS_STATUS_ALL[1]}"
    typeset -g  VCS_STATUS_REMOTE_BRANCH="${VCS_STATUS_ALL[2]}"
    typeset -g  VCS_STATUS_REMOTE_URL="${VCS_STATUS_ALL[3]}"
    typeset -g  VCS_STATUS_ACTION="${VCS_STATUS_ALL[4]}"
    typeset -gi VCS_STATUS_HAS_STAGED="${VCS_STATUS_ALL[5]}"
    typeset -gi VCS_STATUS_HAS_UNSTAGED="${VCS_STATUS_ALL[6]}"
    typeset -gi VCS_STATUS_HAS_UNTRACKED="${VCS_STATUS_ALL[7]}"
    typeset -gi VCS_STATUS_COMMITS_AHEAD="${VCS_STATUS_ALL[8]}"
    typeset -gi VCS_STATUS_COMMITS_BEHIND="${VCS_STATUS_ALL[9]}"
    typeset -gi VCS_STATUS_STASHES="${VCS_STATUS_ALL[10]}"
    break;
  done
}

function gitstatus_init() {
  emulate -L zsh
  setopt err_return no_unset

  [[ ! -v GITSTATUS_DAEMON_PID ]] || return 0

  typeset -gH _GITSTATUS_REQ_FIFO _GITSTATUS_RESP_FIFO
  typeset -giH _GITSTATUS_REQ_FD _GITSTATUS_RESP_FD

  function make_fifos() {
    _GITSTATUS_REQ_FIFO=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.req.XXXXXXXXXX)
    _GITSTATUS_RESP_FIFO=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.resp.XXXXXXXXXX)
    mkfifo $_GITSTATUS_REQ_FIFO
    mkfifo $_GITSTATUS_RESP_FIFO
    exec {_GITSTATUS_REQ_FD}<>$_GITSTATUS_REQ_FIFO
    exec {_GITSTATUS_RESP_FD}<>$_GITSTATUS_RESP_FIFO
  }

  make_fifos || { rm -f $_GITSTATUS_REQ_FIFO $_GITSTATUS_RESP_FIFO && false }

  typeset -g GITSTATUS_DAEMON_LOG
  GITSTATUS_DAEMON_LOG=$(mktemp "${TMPDIR:-/tmp}"/gitstatus.$$.log.XXXXXXXXXX)

  (
    nice -n -20 $GITSTATUS_DAEMON                                            \
      --dirty-max-index-size=$GITSTATUS_DIRTY_MAX_INDEX_SIZE --parent-pid=$$ \
      <&$_GITSTATUS_REQ_FD >&$_GITSTATUS_RESP_FD 2>$GITSTATUS_DAEMON_LOG || true
    rm -f $_GITSTATUS_REQ_FIFO $_GITSTATUS_RESP_FIFO
  ) &!

  typeset -gi GITSTATUS_DAEMON_PID=$!

  local reply
  echo -nE $'hello\x1f\x1e' >&$_GITSTATUS_REQ_FD
  IFS='' read -r -d $'\x1e' -u $_GITSTATUS_RESP_FD -t $GITSTATUS_TIMEOUT_SEC reply
  [[ $reply == $'hello\x1f0' ]]
}

zmodload zsh/datetime || return

if gitstatus_init; then
  typeset -gi GITSTATUS_CLIENT_PID=$$
  # Tell Powerlevel10k that it can now use gitstatus for querying the state of git repos.
  # It'lll make vcs prompt much faster.
  : ${POWERLEVEL9K_VCS_STATUS_COMMAND=gitstatus_query_dir}
else
  echo "gitstatus failed to initialize" >&2
  if [[ -n $GITSTATUS_DAEMON_PID ]]; then
    kill $GITSTATUS_DAEMON_PID &>/dev/null
  fi
  unset GITSTATUS_DAEMON_PID
  [[ -n $_GITSTATUS_REQ_FD ]] && exec {_GITSTATUS_REQ_FD}>&-
  [[ -n $_GITSTATUS_RESP_FD ]] && exec {_GITSTATUS_RESP_FD}>&-
  rm -f $_GITSTATUS_REQ_FIFO $_GITSTATUS_RESP_FIFO
fi

unset -f gitstatus_init
