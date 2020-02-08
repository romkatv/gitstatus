# Copyright 2019 Roman Perepelitsa.
#
# This file is part of GitStatus. It provides fish bindings.
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

function gitstatus_query --description='Retrives status of a git repository'
    set -l IFS
    test -n "$TMPDIR"; or set -l TMPDIR /tmp

    argparse --name=gitstatus_start \
             --max-args=0 \
             'd/dir=' \
             't/timeout=' \
             'p/no-diff' \
             'h/help' \
             -- $argv
    or begin
        _gitstatus_query_usage
        return 1
    end
    if set -q _flag_help
        _gitstatus_query_help
        return 0
    end

    set -q GITSTATUS_DAEMON_PID; or return # not started

    set -l req_id (random).(random).(random).(random)

    set -l dir "."
    set -q _flag_dir; and set dir "$_flag_dir"
    test -n "$GIT_DIR"; and set dir "$GIT_DIR"
    set dir (realpath "$dir")

    set -l timeout
    set -q _flag_timeout; and set timeout -t "$_flag_timeout"

    set -l no_diff 0
    set -q _flag_no_diff; and set no_diff 1

    # Fish currently doesn't support `read` with timeout, use netcat's timeout instead.
    # (See the issue https://github.com/fish-shell/fish-shell/issues/1134)
    set -l resp_fifo (mktemp -u "$TMPDIR/gitstatus.$fish_pid.pipe.tmp.XXXXXXXXXX"); or return
    mkfifo "$resp_fifo"; or return
    set -lx _gitstatus_req $req_id\x1f$dir\x1f$no_diff\x1e
    set -l resp (fish -c 'echo -n $_gitstatus_req; expect -c "expect \x1e"' < "$resp_fifo" |
                 nc -U "$GITSTATUS_SOCK" $timeout 2> /dev/null | tee "$resp_fifo")
    command rm "$resp_fifo"; or return

    test -n "$resp"; or return
    test (string sub -s -1 "$resp") = \x1e; or return
    string trim -rc \x1e "$resp" | read -zad \x1f resp
    test "$resp[1]" = "$req_id"; or return

    if test "$resp[2]" = 1
        set -g VCS_STATUS_RESULT ok-sync
        set -g VCS_STATUS_WORKDIR "$resp[3]"
        set -g VCS_STATUS_COMMIT "$resp[4]"
        set -g VCS_STATUS_LOCAL_BRANCH "$resp[5]"
        set -g VCS_STATUS_REMOTE_BRANCH "$resp[6]"
        set -g VCS_STATUS_REMOTE_NAME "$resp[7]"
        set -g VCS_STATUS_REMOTE_URL "$resp[8]"
        set -g VCS_STATUS_ACTION "$resp[9]"
        set -g VCS_STATUS_INDEX_SIZE "$resp[10]"
        set -g VCS_STATUS_NUM_STAGED "$resp[11]"
        set -g VCS_STATUS_NUM_UNSTAGED "$resp[12]"
        set -g VCS_STATUS_NUM_CONFLICTED "$resp[13]"
        set -g VCS_STATUS_NUM_UNTRACKED "$resp[14]"
        set -g VCS_STATUS_COMMITS_AHEAD "$resp[15]"
        set -g VCS_STATUS_COMMITS_BEHIND "$resp[16]"
        set -g VCS_STATUS_STASHES "$resp[17]"
        set -g VCS_STATUS_TAG "$resp[18]"
        set -g VCS_STATUS_NUM_UNSTAGED_DELETED "$resp[19]"
        set -g VCS_STATUS_NUM_STAGED_NEW 0
        set -q resp[20]; and set VCS_STATUS_NUM_STAGED_NEW "$resp[20]"
        set -g VCS_STATUS_NUM_STAGED_DELETED 0
        set -q resp[21]; and set VCS_STATUS_NUM_STAGED_DELETED "$resp[21]"
        set -g VCS_STATUS_PUSH_REMOTE_NAME "$resp[22]"
        set -g VCS_STATUS_PUSH_REMOTE_URL "$resp[23]"
        set -g VCS_STATUS_PUSH_COMMITS_AHEAD 0
        set -q resp[24]; and set VCS_STATUS_PUSH_COMMITS_AHEAD "$resp[24]"
        set -g VCS_STATUS_PUSH_COMMITS_BEHIND 0
        set -q resp[25]; and set VCS_STATUS_PUSH_COMMITS_BEHIND "$resp[25]"
        set -g VCS_STATUS_HAS_STAGED 0
        test "$VCS_STATUS_NUM_STAGED" -gt 0; and set VCS_STATUS_HAS_STAGED 1
        if test "$GITSTATUS_DIRTY_MAX_INDEX_SIZE" -ge 0 \
            -a "$VCS_STATUS_INDEX_SIZE" -gt "$GITSTATUS_DIRTY_MAX_INDEX_SIZE"
            set -g VCS_STATUS_HAS_UNSTAGED -1
            set -g VCS_STATUS_HAS_CONFLICTED -1
            set -g VCS_STATUS_HAS_UNTRACKED -1
        else
            set -g VCS_STATUS_HAS_UNSTAGED 0
            test "$VCS_STATUS_NUM_UNSTAGED" -gt 0; and set VCS_STATUS_HAS_UNSTAGED 1
            set -g VCS_STATUS_HAS_CONFLICTED 0
            test "$VCS_STATUS_NUM_CONFLICTED" -gt 0; and set VCS_STATUS_HAS_CONFLICTED 1
            set -g VCS_STATUS_HAS_UNTRACKED 0
            test "$VCS_STATUS_NUM_UNTRACKED" -gt 0; and set VCS_STATUS_HAS_UNTRACKED 1
        end
    else
        set -g VCS_STATUS_RESULT norepo-sync
        set -e VCS_STATUS_WORKDIR
        set -e VCS_STATUS_COMMIT
        set -e VCS_STATUS_LOCAL_BRANCH
        set -e VCS_STATUS_REMOTE_BRANCH
        set -e VCS_STATUS_REMOTE_NAME
        set -e VCS_STATUS_REMOTE_URL
        set -e VCS_STATUS_ACTION
        set -e VCS_STATUS_INDEX_SIZE
        set -e VCS_STATUS_NUM_STAGED
        set -e VCS_STATUS_NUM_UNSTAGED
        set -e VCS_STATUS_NUM_CONFLICTED
        set -e VCS_STATUS_NUM_UNTRACKED
        set -e VCS_STATUS_HAS_STAGED
        set -e VCS_STATUS_HAS_UNSTAGED
        set -e VCS_STATUS_HAS_CONFLICTED
        set -e VCS_STATUS_HAS_UNTRACKED
        set -e VCS_STATUS_COMMITS_AHEAD
        set -e VCS_STATUS_COMMITS_BEHIND
        set -e VCS_STATUS_STASHES
        set -e VCS_STATUS_TAG
        set -e VCS_STATUS_NUM_UNSTAGED_DELETED
        set -e VCS_STATUS_NUM_STAGED_NEW
        set -e VCS_STATUS_NUM_STAGED_DELETED
        set -e VCS_STATUS_PUSH_REMOTE_NAME
        set -e VCS_STATUS_PUSH_REMOTE_URL
        set -e VCS_STATUS_PUSH_COMMITS_AHEAD
        set -e VCS_STATUS_PUSH_COMMITS_BEHIND
    end

    return 0
end

function _gitstatus_query_usage
    echo 'Usage: gitstatus_query [-ph] [-d STR] [-t FLOAT] [--help]'
end

function _gitstatus_query_help
    _gitstatus_query_usage
    echo
    echo '  -d STR    Directory to query. Defaults to $PWD. Has no effect if GIT_DIR is set.'
    echo '  -t FLOAT  Timeout in seconds. Will block for at most this long. If no results'
    echo '            are available by then, will return error.'
    echo '  -p        Don\'t compute anything that requires reading Git index. If this option is used,'
    echo '            the following parameters will be 0: VCS_STATUS_INDEX_SIZE,'
    echo '            VCS_STATUS_{NUM,HAS}_{STAGED,UNSTAGED,UNTRACKED,CONFLICTED}.'
    echo
    echo 'On success sets VCS_STATUS_RESULT to one of the following values:'
    echo
    echo '  norepo-sync  The directory doesn\'t belong to a git repository.'
    echo '  ok-sync      The directory belongs to a git repository.'
    echo
    echo 'If VCS_STATUS_RESULT is ok-sync, additional variables are set:'
    echo
    echo '  VCS_STATUS_WORKDIR              Git repo working directory. Not empty.'
    echo '  VCS_STATUS_COMMIT               Commit hash that HEAD is pointing to. Either 40 hex digits or'
    echo '                                  empty if there is no HEAD (empty repo).'
    echo '  VCS_STATUS_LOCAL_BRANCH         Local branch name or empty if not on a branch.'
    echo '  VCS_STATUS_REMOTE_NAME          The remote name, e.g. "upstream" or "origin".'
    echo '  VCS_STATUS_REMOTE_BRANCH        Upstream branch name. Can be empty.'
    echo '  VCS_STATUS_REMOTE_URL           Remote URL. Can be empty.'
    echo '  VCS_STATUS_ACTION               Repository state, A.K.A. action. Can be empty.'
    echo '  VCS_STATUS_INDEX_SIZE           The number of files in the index.'
    echo '  VCS_STATUS_NUM_STAGED           The number of staged changes.'
    echo '  VCS_STATUS_NUM_CONFLICTED       The number of conflicted changes.'
    echo '  VCS_STATUS_NUM_UNSTAGED         The number of unstaged changes.'
    echo '  VCS_STATUS_NUM_UNTRACKED        The number of untracked files.'
    echo '  VCS_STATUS_HAS_STAGED           1 if there are staged changes, 0 otherwise.'
    echo '  VCS_STATUS_HAS_CONFLICTED       1 if there are conflicted changes, 0 otherwise.'
    echo '  VCS_STATUS_HAS_UNSTAGED         1 if there are unstaged changes, 0 if there aren\'t, -1 if'
    echo '                                  unknown.'
    echo '  VCS_STATUS_NUM_STAGED_NEW       The number of staged new files. Note that renamed files'
    echo '                                  are reported as deleted plus new.'
    echo '  VCS_STATUS_NUM_STAGED_DELETED   The number of staged deleted files. Note that renamed files'
    echo '                                  are reported as deleted plus new.'
    echo '  VCS_STATUS_NUM_UNSTAGED_DELETED The number of unstaged deleted files. Note that renamed files'
    echo '                                  are reported as deleted plus new.'
    echo '  VCS_STATUS_HAS_UNTRACKED        1 if there are untracked files, 0 if there aren\'t, -1 if'
    echo '                                  unknown.'
    echo '  VCS_STATUS_COMMITS_AHEAD        Number of commits the current branch is ahead of upstream.'
    echo '                                  Non-negative integer.'
    echo '  VCS_STATUS_COMMITS_BEHIND       Number of commits the current branch is behind upstream.'
    echo '                                  Non-negative integer.'
    echo '  VCS_STATUS_STASHES              Number of stashes. Non-negative integer.'
    echo '  VCS_STATUS_TAG                  The last tag (in lexicographical order) that points to the same'
    echo '                                  commit as HEAD.'
    echo '  VCS_STATUS_PUSH_REMOTE_NAME     The push remote name, e.g. "upstream" or "origin".'
    echo '  VCS_STATUS_PUSH_REMOTE_URL      Push remote URL. Can be empty.'
    echo '  VCS_STATUS_PUSH_COMMITS_AHEAD   Number of commits the current branch is ahead of push remote.'
    echo '                                  Non-negative integer.'
    echo '  VCS_STATUS_PUSH_COMMITS_BEHIND  Number of commits the current branch is behind push remote.'
    echo '                                  Non-negative integer.'
    echo
    echo 'The point of reporting -1 via VCS_STATUS_HAS_* is to allow the command to skip scanning files in'
    echo 'large repos. See -m flag of gitstatus_start.'
    echo
    echo 'gitstatus_query returns an error if gitstatus_start hasn\'t been called in the same'
    echo 'shell or the call had failed.'
end
