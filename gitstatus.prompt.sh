# Copyright 2019 Roman Perepelitsa.
#
# This file is part of GitStatus. It sets Bash PS1 parameter to reflect git status.
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

# Source gitstatus.plugin.sh from $GITSTATUS_DIR or from the same directory
# in which the current script resides if the variable isn't set.
source "${GITSTATUS_DIR:-$(dirname "${BASH_SOURCE[0]}")}/gitstatus.plugin.sh" || return

# Sets GITSTATUS_PROMPT to reflect the state of the current git repository.
# The value is empty if not in a git repository. Forwards all arguments to
# gitstatus_query.
#
# Example value of GITSTATUS_PROMPT:
#
#   master+!? ⇡2 ⇣3 *4
#
# Meaning:
#
#   master   current branch
#   +        git repo has changes staged for commit
#   !        git repo has unstaged changes
#   ?        git repo has untracked files
#   ⇡2       local branch is ahead of origin by 2 commits
#   ⇣3       local branch is behind origin by 3 commits
#   *4       git repo has 4 stashes
function gitstatus_prompt_update() {
  GITSTATUS_PROMPT=""

  gitstatus_query "$@"                  || return 1  # error
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
gitstatus_stop && gitstatus_start

# On every prompt, fetch git status and set GITSTATUS_PROMPT.
PROMPT_COMMAND=gitstatus_prompt_update

# Enable promptvars so that ${GITSTATUS_PROMPT} in PS1 is expanded.
shopt -s promptvars

# Customize prompt. Put $GITSTATUS_PROMPT in it reflect git status.
#
# Example:
#
#   user@host ~/projects/skynet master+!
#   $ █
PS1='\[\033[01;32m\]\u@\h\[\033[00m\] '           # green user@host
PS1+='\[\033[01;34m\]\w\[\033[00m\]'              # blue current working directory
PS1+='${GITSTATUS_PROMPT:+ $GITSTATUS_PROMPT}'    # git status (requires promptvars option)
PS1+='\n\[\033[01;$((31+!$?))m\]\$\[\033[00m\] '  # green/red (success/error) $/# (normal/root)
PS1+='\[\e]0;\u@\h: \w\a\]'                       # terminal title: user@host: dir
