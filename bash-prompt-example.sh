# This is an example of using gitstatus in bash prompt.
#
# Usage:
#
#   git clone https://github.com/romkatv/gitstatus.git ~/gitstatus
#   echo 'source ~/gitstatus/bash-prompt-example.sh' >> ~/.bashrc
#
# Example prompt:
#
#   ~/powerlevel10k master+!
#   ❯
#
# Meaning:
#
#   * ~/powerlevel10k -- current directory
#   * master          -- current git branch
#   * +               -- git repo has changes staged for commit
#   * !               -- git repo has unstaged changes
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
#
# In addition, running `exec bash` (or `exec anything` really) from the shell will
# result in a stranded `gitstatusd` in the background.
#
# A more complete port of the gitstatus ZSH API to bash would be a most welcome
# contribution.

# Simplistic translation to bash of gitstatus_start from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_start() {
  [[ "$GITSTATUS_REQ_FD" -gt 0 && "$GITSTATUS_RESP_FD" -gt 0 ]] && return  # already started

  unset GITSTATUS_REQ_FD GITSTATUS_RESP_FD

  local daemon="$GITSTATUS_DAEMON"
  if [[ -z "$daemon" ]]; then
    local os && os=$(uname -s) && [[ -n "$os" ]]       || return
    local arch && arch=$(uname -m) && [[ -n "$arch" ]] || return
    local dir && dir=$(dirname "${BASH_SOURCE[0]}")    || return
    daemon="$dir/bin/gitstatusd-${os,,}-${arch,,}"
  fi

  [[ -f "$daemon" ]] || return

  local threads="$GITSTATUS_NUM_THREADS"
  if [[ ! "$threads" -gt 0 ]]; then
    case "$(uname -s)" in
      FreeBSD) threads=$(sysctl -n hw.ncpu)         || return;;
      *)       threads=$(getconf _NPROCESSORS_ONLN) || return;;
    esac
    (( threads *= 2 ))
    [[ "$threads" -le 32 ]] || threads=32
  fi

  local req_fifo resp_fifo daemon_pid reply                                    &&
    req_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.req.XXXXXXXXXX)   &&
    resp_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.resp.XXXXXXXXXX) &&
    mkfifo "$req_fifo" "$resp_fifo"                                            &&
    exec {GITSTATUS_REQ_FD}<>"$req_fifo" {GITSTATUS_RESP_FD}<>"$resp_fifo"     &&
    rm "$req_fifo" "$resp_fifo"                                                &&
    { "$daemon" --sigwinch-pid=$$ --num-threads="$threads" \
        <&$GITSTATUS_REQ_FD >&$GITSTATUS_RESP_FD 2>/dev/null & }               &&
    disown %1                                                                  &&
    daemon_pid=$!                                                              &&
    echo -nE $'hello\x1f\x1e' >&$GITSTATUS_REQ_FD                              &&
    IFS='' read -rd $'\x1e' -u $GITSTATUS_RESP_FD -t5 reply                    &&
    [[ "$reply" == $'hello\x1f0' ]] || {
      [[ "$daemon_pid" -gt 0 ]]        && kill "$daemon_pid" &>/dev/null
      [[ "$GITSTATUS_REQ_FD" -gt 0 ]]  && exec {GITSTATUS_REQ_FD}>&-
      [[ "$GITSTATUS_RESP_FD" -gt 0 ]] && exec {GITSTATUS_RESP_FD}>&-
      [[ -n "$req_fifo" ]]             && rm -f "$req_fifo"
      [[ -n "$resp_fifo" ]]            && rm -f "$resp_fifo"
      unset GITSTATUS_REQ_FD GITSTATUS_RESP_FD
      return 1
    }
}

# Simplistic translation to bash of gitstatus_query from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_query() {
  [[ "$GITSTATUS_REQ_FD" -gt 0 && "$GITSTATUS_RESP_FD" -gt 0 ]] || return  # not started

  local req_id="$RANDOM.$RANDOM.$RANDOM.$RANDOM"
  local dir="${GIT_DIR:-$PWD}"
  [[ "$dir" == /* ]] || dir="$PWD/$dir"
  echo -nE "$req_id"$'\x1f'"$dir"$'\x1e' >&$GITSTATUS_REQ_FD || return  # error

  local -a resp
  while true; do
    IFS=$'\x1f' read "$@" -rd $'\x1e' -a resp -u $GITSTATUS_RESP_FD || return  # error
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
# Sets GIT_PROMPT to reflect the state of the current git repository (empty if not in
# a git repository).
function update_git_prompt() {
  GIT_PROMPT=""

  # Call gitstatus_query synchronously with 5s timeout.
  gitstatus_query -t 5                  || return 1  # error
  [[ "$VCS_STATUS_RESULT" == ok-sync ]] || return 0  # not a git repo

  GIT_PROMPT+=" "
  GIT_PROMPT+="${VCS_STATUS_LOCAL_BRANCH:-@${VCS_STATUS_COMMIT}}"
  [[ -n "$VCS_STATUS_TAG" ]] && GIT_PROMPT+="#${VCS_STATUS_TAG}"
  [[ "$VCS_STATUS_HAS_STAGED" == 1 ]] && GIT_PROMPT+="+"
  [[ "$VCS_STATUS_HAS_UNSTAGED" == 1 ]] && GIT_PROMPT+="!"
  [[ "$VCS_STATUS_HAS_UNTRACKED" == 1 ]] && GIT_PROMPT+="?"
  [[ "$VCS_STATUS_COMMITS_AHEAD" -gt 0 ]] && GIT_PROMPT+=" ⇡${VCS_STATUS_COMMITS_AHEAD}"
  [[ "$VCS_STATUS_COMMITS_BEHIND" -gt 0 ]] && GIT_PROMPT+=" ⇣${VCS_STATUS_COMMITS_BEHIND}"
  [[ "$VCS_STATUS_STASHES" -gt 0 ]] && GIT_PROMPT+=" *${VCS_STATUS_STASHES}"
}

# Start gitstatusd in the background.
gitstatus_start

shopt -s promptvars
PROMPT_COMMAND=update_git_prompt
PS1='\w${GIT_PROMPT}\n❯ '
