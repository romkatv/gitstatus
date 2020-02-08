# Copyright 2019 Roman Perepelitsa.
#
# This file is part of GitStatus. It print prompt for fish to reflect git status.
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

# Print GITSTATUS_PROMPT to reflect the state of the current git repository.
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

function gitstatus_prompt
    set -l IFS

    gitstatus_query $argv; or return 1 # error
    test "$VCS_STATUS_RESULT" = ok-sync; or return 0 # not a git repo

    set -l color_reset normal
    set -l color_clean green
    set -l color_untracked cyan
    set -l color_modified yellow
    set -l color_conflicted red

    set_color $color_reset
    echo -n ' '

    set -l where
    if test -n "$VCS_STATUS_LOCAL_BRANCH"
        set where "$VCS_STATUS_LOCAL_BRANCH"
    else if test -n "$VCS_STATUS_TAG"
        set_color $color_reset
        echo -n '#'
        set where "$VCS_STATUS_TAG"
    else
        set_color $color_reset
        echo -n '@'
        set where (string sub -l 8 "$VCS_STATUS_COMMIT")
    end

    if test (string length "$where") -gt 32
        # truncate long branch names and tags
        set where (string sub -l 12 "$where")…(string sub -s -12 "$where")
    end

    set_color $color_clean
    echo -n $where

    # ⇣42 if behind the remote.
    if test "$VCS_STATUS_COMMITS_BEHIND" -gt 0
        set_color $color_clean
        echo -n " ⇣$VCS_STATUS_COMMITS_BEHIND"
    end
    # ⇡42 if ahead of the remote; no leading space if also behind the remote: ⇣42⇡42.
    if test "$VCS_STATUS_COMMITS_AHEAD" -gt 0
        test "$VCS_STATUS_COMMITS_BEHIND" -gt 0; or echo -n ' '
        set_color $color_clean
        echo -n "⇡$VCS_STATUS_COMMITS_AHEAD"
    end
    # *42 if have stashes.
    if test "$VCS_STATUS_STASHES" -gt 0
        set_color $color_clean
        echo -n " *$VCS_STATUS_STASHES"
    end
    # 'merge' if the repo is in an unusual state.
    if test -n "$VCS_STATUS_ACTION"
        set_color $color_conflicted
        echo -n " $VCS_STATUS_ACTION"
    end
    # ~42 if have merge conflicts.
    if test "$VCS_STATUS_NUM_CONFLICTED" -gt 0
        set_color $color_conflicted
        echo -n " ~$VCS_STATUS_NUM_CONFLICTED"
    end
    # +42 if have staged changes.
    if test "$VCS_STATUS_NUM_STAGED" -gt 0
        set_color $color_modified
        echo -n " +$VCS_STATUS_NUM_STAGED"
    end
    # !42 if have unstaged changes.
    if test "$VCS_STATUS_NUM_UNSTAGED" -gt 0
        set_color $color_modified
        echo -n " !$VCS_STATUS_NUM_UNSTAGED"
    end
    # ?42 if have untracked files. It's really a question mark, your font isn't broken.
    if test "$VCS_STATUS_NUM_UNTRACKED" -gt 0
        set_color $color_untracked
        echo -n " !$VCS_STATUS_NUM_UNTRACKED"
    end

    set_color $color_reset
    return 0
end

# Start gitstatusd in the background.
gitstatus_stop
gitstatus_start -s -1 -u -1 -c -1 -d -1
