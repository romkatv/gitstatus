# This is an example of using gitstatus in bash prompt.
#
# Usage:
#
#   git clone https://github.com/romkatv/gitstatus.git ~/gitstatus
#   echo 'source ~/gitstatus/bash-prompt-example.sh' >> ~/.bashrc
#
# Example prompt:
#
#   ~/powerlevel10k master+!? ⇡2 ⇣3 *4
#   ❯
#
# Meaning:
#
#   * ~/powerlevel10k -- current directory
#   * master          -- current git branch
#   * +               -- git repo has changes staged for commit
#   * !               -- git repo has unstaged changes
#   * ?               -- git repo has untracked files
#   * ⇡2              -- local branch is ahead of origin by 2 commits
#   * ⇣3              -- local branch is behind origin by 3 commits
#   * *4              -- git repo has 4 stashes
#
# This code uses `gitstatusd` directly, whose API has no backward compatibility
# guarantees.
#
# Missing features compared to the official ZSH API provided by gitstatus:
#
#   * Async API.
#   * Multiple gitstatusd instances.
#   * Logging.
#   * gitstatus_start options: -t and -m.
#   * gitstatus_query options: -d and -c.

# Partial translation to bash of gitstatus_start from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_start() {
  [[ -z "${GITSTATUS_DAEMON_PID:-}" ]] || return 0  # already started

  local req_fifo resp_fifo

  function gitstatus_start_impl() {
    local daemon="${GITSTATUS_DAEMON:-}"
    if [[ -z "$daemon" ]]; then
      local os   &&   os=$(uname -s)                    || return
      local arch && arch=$(uname -m)                    || return
      local dir  &&  dir=$(dirname "${BASH_SOURCE[0]}") || return
      daemon="$dir/bin/gitstatusd-${os,,}-${arch,,}"
    fi

    local threads="${GITSTATUS_NUM_THREADS:-0}"
    if (( threads <= 0 )); then
      case "$(uname -s)" in
        FreeBSD) threads=$(sysctl -n hw.ncpu)         || return;;
        *)       threads=$(getconf _NPROCESSORS_ONLN) || return;;
      esac
      (( threads *=  2 ))
      (( threads >=  2 )) || threads=2
      (( threads <= 32 )) || threads=32
    fi

    req_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.req.XXXXXXXXXX)   || return
    resp_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.resp.XXXXXXXXXX) || return
    mkfifo "$req_fifo" "$resp_fifo"                                            || return
    exec {GITSTATUS_REQ_FD}<>"$req_fifo" {GITSTATUS_RESP_FD}<>"$resp_fifo"     || return
    command rm "$req_fifo" "$resp_fifo"                                        || return

    { <&$GITSTATUS_REQ_FD >&$GITSTATUS_RESP_FD 2>/dev/null bash -c "
        ${daemon@Q} --sigwinch-pid=$$ --num-threads=$threads
        echo -nE $'bye\x1f0\x1e'" & } 2>/dev/null
    disown
    GITSTATUS_DAEMON_PID=$!

    local reply
    echo -nE $'hello\x1f\x1e' >&$GITSTATUS_REQ_FD           || return
    IFS='' read -rd $'\x1e' -u $GITSTATUS_RESP_FD -t5 reply || return
    [[ "$reply" == $'hello\x1f0' ]]                         || return
  }

  if ! gitstatus_start_impl; then
    echo "gitstatus failed to initialize" >&2
    [[ -z "${GITSTATUS_REQ_FD:-}"     ]] || exec {GITSTATUS_REQ_FD}>&-
    [[ -z "${GITSTATUS_RESP_FD:-}"    ]] || exec {GITSTATUS_RESP_FD}>&-
    [[ -z "${req_fifo:-}"             ]] || command rm -f "$req_fifo"
    [[ -z "${resp_fifo:-}"            ]] || command rm -f "$resp_fifo"
    [[ -z "${GITSTATUS_DAEMON_PID:-}" ]] || kill -- -"$GITSTATUS_DAEMON_PID" &>/dev/null
    unset GITSTATUS_DAEMON_PID GITSTATUS_REQ_FD GITSTATUS_RESP_FD
    unset -f gitstatus_start_impl
    return 1
  fi

  unset -f gitstatus_start_impl

  function _gitstatus_exec() {
    if [[ $# -gt 0 && -n "${GITSTATUS_DAEMON_PID:-}" ]]; then
      exec {GITSTATUS_REQ_FD}>&- {GITSTATUS_RESP_FD}>&-
      kill -- -"$GITSTATUS_DAEMON_PID" &>/dev/null || true
      unalias exec builtin &>/dev/null             || true
      unset -f _gitstatus_exec _gitstatus_builtin
      unset GITSTATUS_DAEMON_PID GITSTATUS_REQ_FD GITSTATUS_RESP_FD
    fi
    exec "$@"
    local ret=$?
    [[ -n "${GITSTATUS_DAEMON_PID:-}" ]] || gitstatus_start || true
    return $ret
  }
  alias exec=_gitstatus_exec

  function _gitstatus_builtin() {
    while [[ "${1:-}" == builtin ]]; do shift; done
    [[ "${1:-}" != exec ]] || set -- _gitstatus_exec "${@:2}"
    "$@"
  }
  alias builtin=_gitstatus_builtin
}

# Partial translation to bash of gitstatus_query from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_query() {
  [[ -n "$GITSTATUS_DAEMON_PID" ]] || return  # not started

  local req_id="$RANDOM.$RANDOM.$RANDOM.$RANDOM"
  local dir="${GIT_DIR:-$PWD}"
  [[ "$dir" == /* ]] || dir="$PWD/$dir"
  echo -nE "$req_id"$'\x1f'"$dir"$'\x1e' >&$GITSTATUS_REQ_FD || return

  local -a resp
  while true; do
    IFS=$'\x1f' read "$@" -rd $'\x1e' -a resp -u $GITSTATUS_RESP_FD || return
    [[ "${resp[0]}" == "$req_id" ]] && break
  done

  if [[ "${resp[1]}" == 1 ]]; then
    VCS_STATUS_RESULT=ok-sync
    VCS_STATUS_WORKDIR="${resp[2]}"
    VCS_STATUS_COMMIT="${resp[3]}"
    VCS_STATUS_LOCAL_BRANCH="${resp[4]}"
    VCS_STATUS_REMOTE_BRANCH="${resp[5]}"
    VCS_STATUS_REMOTE_NAME="${resp[6]}"
    VCS_STATUS_REMOTE_URL="${resp[7]}"
    VCS_STATUS_ACTION="${resp[8]}"
    VCS_STATUS_HAS_STAGED="${resp[9]}"
    VCS_STATUS_HAS_UNSTAGED="${resp[10]}"
    VCS_STATUS_HAS_UNTRACKED="${resp[11]}"
    VCS_STATUS_COMMITS_AHEAD="${resp[12]}"
    VCS_STATUS_COMMITS_BEHIND="${resp[13]}"
    VCS_STATUS_STASHES="${resp[14]}"
    VCS_STATUS_TAG="${resp[15]}"
  else
    VCS_STATUS_RESULT=norepo-sync
    unset VCS_STATUS_WORKDIR
    unset VCS_STATUS_COMMIT
    unset VCS_STATUS_LOCAL_BRANCH
    unset VCS_STATUS_REMOTE_BRANCH
    unset VCS_STATUS_REMOTE_NAME
    unset VCS_STATUS_REMOTE_URL
    unset VCS_STATUS_ACTION
    unset VCS_STATUS_HAS_STAGED
    unset VCS_STATUS_HAS_UNSTAGED
    unset VCS_STATUS_HAS_UNTRACKED
    unset VCS_STATUS_COMMITS_AHEAD
    unset VCS_STATUS_COMMITS_BEHIND
    unset VCS_STATUS_STASHES
    unset VCS_STATUS_TAG
  fi
}

# This is an example showing how gitstatus_query can be used to put git info into PS1.
#
# Sets GITSTATUS_PROMPT to reflect the state of the current git repository (empty if not in
# a git repository).
function update_git_prompt() {
  GITSTATUS_PROMPT=""

  # Call gitstatus_query synchronously with 5s timeout.
  gitstatus_query -t 5                  || return 1  # error
  [[ "$VCS_STATUS_RESULT" == ok-sync ]] || return 0  # not a git repo

  local     reset=$'\e[0m'         # no color
  local     clean=$'\e[38;5;076m'  # green foreground
  local untracked=$'\e[38;5;014m'  # teal foreground
  local  modified=$'\e[38;5;011m'  # yellow foreground

  local p
  if (( VCS_STATUS_HAS_STAGED || VCS_STATUS_HAS_UNSTAGED )); then
    p+="$modified"
  elif (( VCS_STATUS_HAS_UNTRACKED )); then
    p+="$untracked"
  else
    p+="$clean"
  fi
  p+="${VCS_STATUS_LOCAL_BRANCH:-@${VCS_STATUS_COMMIT}}"

  [[ -n "$VCS_STATUS_TAG"               ]] && p+="#${VCS_STATUS_TAG}"
  [[ "$VCS_STATUS_HAS_STAGED"      == 1 ]] && p+="${modified}+"
  [[ "$VCS_STATUS_HAS_UNSTAGED"    == 1 ]] && p+="${modified}!"
  [[ "$VCS_STATUS_HAS_UNTRACKED"   == 1 ]] && p+="${untracked}?"
  [[ "$VCS_STATUS_COMMITS_AHEAD"  -gt 0 ]] && p+="${clean} ⇡${VCS_STATUS_COMMITS_AHEAD}"
  [[ "$VCS_STATUS_COMMITS_BEHIND" -gt 0 ]] && p+="${clean} ⇣${VCS_STATUS_COMMITS_BEHIND}"
  [[ "$VCS_STATUS_STASHES"        -gt 0 ]] && p+="${clean} *${VCS_STATUS_STASHES}"

  GITSTATUS_PROMPT="${reset}${p}${reset}"
}

# Start gitstatusd in the background.
gitstatus_start

shopt -s promptvars
PROMPT_COMMAND=update_git_prompt
PS1='\w${GITSTATUS_PROMPT:+ }${GITSTATUS_PROMPT}\n❯ '
