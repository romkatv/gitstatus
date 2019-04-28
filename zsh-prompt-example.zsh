# Copyright 2019 Roman Perepelitsa.
#
# This file is part of GitStatus. It sets ZSH PROMPT parameter to reflect git status.
#
# GitStatus is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# GitStatus is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with GitStatus. If not, see <https://www.gnu.org/licenses/>.

# Source gitstatus.plugin.zsh from $GITSTATUS_DIR or from the same directory
# in which the current script resides if the variable isn't set.
source ${GITSTATUS_DIR:-${${(%):-%x}:h}}/gitstatus.plugin.zsh || return

# Sets GITSTATUS_PROMPT to reflect the state of the current git repository (empty if not
# in a git repository).
function gitstatus_prompt_update() {
  emulate -L zsh
  typeset -g GITSTATUS_PROMPT=""

  # Call gitstatus_query synchronously. Note that gitstatus_query can also be called
  # asynchronously; see documentation in gitstatus.plugin.zsh.
  gitstatus_query MY                  || return 1  # error
  [[ $VCS_STATUS_RESULT == ok-sync ]] || return 0  # not a git repo

  local     reset='%f'       # no foreground
  local     clean='%F{076}'  # green foreground
  local untracked='%F{014}'  # teal foreground
  local  modified='%F{011}'  # yellow foreground

  local p
  if (( VCS_STATUS_HAS_STAGED || VCS_STATUS_HAS_UNSTAGED )); then
    p+=$modified
  elif (( VCS_STATUS_HAS_UNTRACKED )); then
    p+=$untracked
  else
    p+=$clean
  fi
  p+=${VCS_STATUS_LOCAL_BRANCH:-@${VCS_STATUS_COMMIT}}

  [[ -n $VCS_STATUS_TAG               ]] && p+="#${VCS_STATUS_TAG}"
  [[ $VCS_STATUS_HAS_STAGED      == 1 ]] && p+="${modified}+"
  [[ $VCS_STATUS_HAS_UNSTAGED    == 1 ]] && p+="${modified}!"
  [[ $VCS_STATUS_HAS_UNTRACKED   == 1 ]] && p+="${untracked}?"
  [[ $VCS_STATUS_COMMITS_AHEAD  -gt 0 ]] && p+="${clean} ⇡${VCS_STATUS_COMMITS_AHEAD}"
  [[ $VCS_STATUS_COMMITS_BEHIND -gt 0 ]] && p+="${clean} ⇣${VCS_STATUS_COMMITS_BEHIND}"
  [[ $VCS_STATUS_STASHES        -gt 0 ]] && p+="${clean} *${VCS_STATUS_STASHES}"

  GITSTATUS_PROMPT="${reset}${p}${reset}"
}

# Start gitstatusd instance with name "MY". The same name is passed to
# gitstatus_query in gitstatus_prompt_update.
gitstatus_start MY

# On every prompt, fetch git status and set GITSTATUS_PROMPT.
autoload -Uz add-zsh-hook
add-zsh-hook precmd gitstatus_prompt_update

# Enable promptsubst so that ${GITSTATUS_PROMPT} in PROMPT is expanded.
setopt promptsubst

# Customize prompt. Put $GITSTATUS_PROMPT in it reflect git status.
#
# Example:
#
#   user@host ~/projects/skynet master+!
#   % █
PROMPT='%F{002}%n@%m%f '                           # green user@host
PROMPT+='%F{039}%~%f'                              # bright blue current working directory
PROMPT+='${GITSTATUS_PROMPT:+ $GITSTATUS_PROMPT}'  # git status (requires promptsubst option)
PROMPT+=$'\n%F{00$((2+7*!!$?))}%#%f '              # green/red (success/error) %/# (normal/root)
