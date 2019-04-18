# This is an example of using gitstatus in bash prompt.
#
# Usage:
#
#   git clone https://github.com/romkatv/gitstatus.git ~/gitstatus
#   echo 'source ~/gitstatus/bash-prompt-example.sh' >> ~/.bashrc
#
# This code uses `gitstatusd` directly, whose API has no backward compatibility
# guarantees.
#
# Missing features compared to the official ZSH API provided by gitstatus:
#
#   * Error handling.
#   * Self-check on initialization.
#   * Async API.
#   * Multiple gitstatusd instances.
#   * Relative $GIT_DIR support.
#
# In addition, running `exec bash` (or `exec anything` really) from the shell will
# result in a stranded `gitstatusd` in the background.
#
# A more complete port of the gitstatus ZSH API to bash would be a most welcome
# contribution.

# Simplistic translation to bash of gitstatus_start from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_start() {
  local os=$(uname -s)
  local arch=$(uname -m)
  local dir=$(dirname "${BASH_SOURCE[0]}")
  local daemon="${GITSTATUS_DAEMON:-$dir/bin/gitstatusd-${os,,}-${arch,,}}"

  case $os in
    FreeBSD) local threads=$(( 2 * $(sysctl -n hw.ncpu) ));;
    *)       local threads=$(( 2 * $(getconf _NPROCESSORS_ONLN) ));;
  esac

  [[ "$threads" -le 32 ]] || threads=32

  local req_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.req.XXXXXXXXXX)
  local resp_fifo=$(mktemp -u "${TMPDIR:-/tmp}"/gitstatus.$$.pipe.resp.XXXXXXXXXX)

  mkfifo "$req_fifo"
  mkfifo "$resp_fifo"

  exec {GITSTATUS_REQ_FD}<>"$req_fifo"
  exec {GITSTATUS_RESP_FD}<>"$resp_fifo"

  rm "$req_fifo"
  rm "$resp_fifo"

  "$daemon" --sigwinch-pid=$$ --num-threads="$threads" \
      <&$GITSTATUS_REQ_FD >&$GITSTATUS_RESP_FD 2>/dev/null &
  disown %1
}

# Simplistic translation to bash of gitstatus_query from the official gitstatus ZSH API.
# See https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh for documentation.
function gitstatus_query() {
  local -a resp
  echo -nE $'\x1f'"${GIT_DIR:-$PWD}"$'\x1e' >&$GITSTATUS_REQ_FD
  IFS=$'\x1f' read -t5 -rd $'\x1e' -a resp -u $GITSTATUS_RESP_FD

  if [[ "${resp[1]}" != 1 ]]; then
    VCS_STATUS_RESULT=norepo-sync
    return
  fi

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
}

function gitstatus_prompt() {
  shopt -u promptvars
  PS1="\w"

  gitstatus_query
  if [[ "$VCS_STATUS_RESULT" == ok-sync ]]; then
    PS1+=" "
    PS1+="${VCS_STATUS_LOCAL_BRANCH:-@${VCS_STATUS_COMMIT}}"
    [[ -n "$VCS_STATUS_TAG" ]] && PS1+="#${VCS_STATUS_TAG}"
    [[ "$VCS_STATUS_HAS_STAGED" == 1 ]] && PS1+="+"
    [[ "$VCS_STATUS_HAS_UNSTAGED" == 1 ]] && PS1+="!"
    [[ "$VCS_STATUS_HAS_UNTRACKED" == 1 ]] && PS1+="?"
    [[ "$VCS_STATUS_COMMITS_AHEAD" -gt 0 ]] && PS1+=" ⇡${VCS_STATUS_COMMITS_AHEAD}"
    [[ "$VCS_STATUS_COMMITS_BEHIND" -gt 0 ]] && PS1+=" ⇣${VCS_STATUS_COMMITS_BEHIND}"
    [[ "$VCS_STATUS_STASHES" -gt 0 ]] && PS1+=" *${VCS_STATUS_STASHES}"
  fi

  PS1+='\n❯ '
}

gitstatus_start
unset -f gitstatus_start

PROMPT_COMMAND=gitstatus_prompt
