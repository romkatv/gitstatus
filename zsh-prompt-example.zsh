# This example shows how gitstatus can be used to put git info into ZSH PROMPT.
#
# Usage:
#
#   git clone https://github.com/romkatv/gitstatus.git ~/gitstatus
#   echo 'source ~/gitstatus/zsh-prompt-example.zsh' >> ~/.zshrc
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

# Source gitstatus.plugin.zsh from $GITSTATUS_DIR if it's set or
# from the same directory where the current script resides otherwise.
source ${GITSTATUS_DIR:-${${(%):-%x}:h}}/gitstatus.plugin.zsh

# Sets GIT_PROMPT to reflect the state of the current git repository (empty if not
# in a git repository).
function update_git_prompt() {
  emulate -L zsh
  typeset -g GIT_PROMPT=""

  # Call gitstatus_query synchronously with 5s timeout. Note that gitstatus_query
  # can also be called asynchronously; see documentation in gitstatus.plugin.zsh
  gitstatus_query -t 5 MY             || return 1  # error
  [[ $VCS_STATUS_RESULT == ok-sync ]] || return 0  # not a git repo

  GIT_PROMPT+=" "
  GIT_PROMPT+="${VCS_STATUS_LOCAL_BRANCH:-@${VCS_STATUS_COMMIT}}"
  [[ -n $VCS_STATUS_TAG ]] && GIT_PROMPT+="#${VCS_STATUS_TAG}"
  [[ $VCS_STATUS_HAS_STAGED == 1 ]] && GIT_PROMPT+="+"
  [[ $VCS_STATUS_HAS_UNSTAGED == 1 ]] && GIT_PROMPT+="!"
  [[ $VCS_STATUS_HAS_UNTRACKED == 1 ]] && GIT_PROMPT+="?"
  [[ $VCS_STATUS_COMMITS_AHEAD -gt 0 ]] && GIT_PROMPT+=" ⇡${VCS_STATUS_COMMITS_AHEAD}"
  [[ $VCS_STATUS_COMMITS_BEHIND -gt 0 ]] && GIT_PROMPT+=" ⇣${VCS_STATUS_COMMITS_BEHIND}"
  [[ $VCS_STATUS_STASHES -gt 0 ]] && GIT_PROMPT+=" *${VCS_STATUS_STASHES}"
}

# Start gitstatusd instance with name "MY". The same name is passed to
# gitstatus_query in update_git_prompt.
gitstatus_start MY

autoload -Uz add-zsh-hook
add-zsh-hook precmd update_git_prompt
setopt nopromptbang prompt{cr,percent,sp,subst}
PROMPT=$'%~${GIT_PROMPT}\n❯ '
