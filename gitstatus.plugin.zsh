# Return an error from gitstatus_query_dir after this many seconds.
: ${GITSTATUS_TIMEOUT_SEC=5}

# Path to gitstatusd. Default to gitstatusd in the same directory as this file.
: ${GITSTATUS_DAEMON=${${(%):-%x}:A:h}/gitstatusd}

# Report -1 unstaged and untracked if there are more than this many files in the index; negative
# value means infinity.
: ${GITSTATUS_DIRTY_MAX_INDEX_SIZE=-1}

# Retrives status of a git repo from a directory under its working tree.
#
#   $1 -- Directory to query. Defaults to $PWD.
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

  echo -nE "${1-"${PWD}"}"$'\x1e' >&$_GITSTATUS_REQ

  typeset -g VCS_STATUS_ALL
  IFS=$'\x1f' read -rd $'\x1e' -u $_GITSTATUS_RESP -t $GITSTATUS_TIMEOUT_SEC -A VCS_STATUS_ALL || {
    echo "gitstatus: timed out" >&2
    return 1
  }

  [[ "${VCS_STATUS_ALL[1]}" == 1 ]]

  shift VCS_STATUS_ALL
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
}

function gitstatus_init() {
  emulate -L zsh
  setopt err_return no_unset

  [[ ! -v GITSTATUS_DAEMON_PID ]] || return 0

  function make_fifo() {
    local FIFO
    FIFO=$(mktemp -tu gitstatus.$$.pipe.XXXXXXXXXX)
    mkfifo $FIFO
    eval "exec {$1}<>${(q)FIFO}"
    rm $FIFO
  }

  typeset -gH _GITSTATUS_REQ _GITSTATUS_RESP
  make_fifo _GITSTATUS_REQ
  make_fifo _GITSTATUS_RESP

  typeset -g GITSTATUS_DAEMON_LOG
  GITSTATUS_DAEMON_LOG=$(mktemp -t gitstatus.$$.log.XXXXXXXXXX)

  nice -n -20 $GITSTATUS_DAEMON                                            \
    --dirty-max-index-size=$GITSTATUS_DIRTY_MAX_INDEX_SIZE --parent-pid=$$ \
    <&$_GITSTATUS_REQ >&$_GITSTATUS_RESP 2>$GITSTATUS_DAEMON_LOG &!

  typeset -g GITSTATUS_DAEMON_PID=$!

  local reply
  echo -nE $'\x1e' >&$_GITSTATUS_REQ
  IFS='' read -r -d $'\x1e' -u $_GITSTATUS_RESP -t $GITSTATUS_TIMEOUT_SEC reply
  [[ $reply == 0 ]]
}

if ! gitstatus_init; then
  echo "gitstatus failed to initialize" >&2
  unset GITSTATUS_DAEMON_PID
fi

unset -f gitstatus_init
