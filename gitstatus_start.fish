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

functions -q gitstatus_stop
function gitstatus_start --description='Starts gitstatusd in the background'
    set -l IFS
    test -n "$TMPDIR"; or set -l TMPDIR /tmp

    argparse --name=gitstatus_start \
             --max-args=0 \
             't/timeout=' \
             's/max-num-staged=' \
             'u/max-num-unstaged=' \
             'c/max-num-conflicted=' \
             'd/max-num-untracked=' \
             'm/max-dirty=' \
             'e/recurse-untracked-dirs' \
             'U/ignore-status-show-untracked-files' \
             'W/ignore-bash-show-untracked-files' \
             'D/ignore-bash-show-dirty-state' \
             'h/help' \
             -- $argv
    or begin
        _gitstatus_start_usage
        return 1
    end
    if set -q _flag_help
        _gitstatus_start_help
        return 0
    end

    set -q _flag_timeout; or set -l _flag_timeout 5
    set -q _flag_max_num_staged; or set -l _flag_max_num_staged 1
    set -q _flag_max_num_unstaged; or set -l _flag_max_num_unstaged 1
    set -q _flag_max_num_conflicted; or set -l _flag_max_num_conflicted 1
    set -q _flag_max_num_untracked; or set -l _flag_max_num_untracked 1
    set -q _flag_max_dirty; or set -l _flag_max_dirty -1

    set -q GITSTATUS_DAEMON_PID
    and return # already started

    set -q GITSTATUS_DIR
    or set -l GITSTATUS_DIR (dirname (realpath (status filename)))

    function gitstatus_start_impl --no-scope-shadowing
        set -l log_level "$GITSTATUS_LOG_LEVEL"
        if test -z "$log_level" -a "$GITSTATUS_ENABLE_LOGGING" = 1
            set log_level INFO
        end

        set -l daemon "$GITSTATUS_DAEMON"
        set -l os (uname -s); or return
        if test -z "$daemon"
            set -l arch (uname -m); or return
            if test "$os" = Linux
                set -l uname_o (uname -o)
                if test "$uname_o" = Android
                    set os Android
                end
            end
            if string match -qri '(msys|mingw).*' "$os"
                set os msys_nt-10.0
            end
            set daemon "$GITSTATUS_DIR/bin/gitstatusd-"(string lower "$os")-(string lower "$arch")
        end

        set -l threads "$GITSTATUS_NUM_THREADS"
        if test -z "$threads" -o "$threads" -le 0
            switch $os
                case FreeBSD
                    set threads (sysctl -n hw.ncpu)
                case '*'
                    set threads (getconf _NPROCESSORS_ONLN)
            end
            set threads (math "$threads * 2")
            test "$threads" -ge 2; or set threads 2
            test "$threads" -le 32; or set threads 32
        end

        set -l daemon_args \
            --parent-pid="$fish_pid" \
            --num-threads="$threads" \
            --max-num-staged="$_flag_max_num_staged" \
            --max-num-unstaged="$_flag_max_num_unstaged" \
            --max-num-untracked="$_flag_max_num_untracked" \
            --dirty-max-index-size="$_flag_max_dirty"
        if set -q _flag_recurse_untracked_dirs
            set daemon_args $daemon_args --recurse-untracked-dirs
        end
        if set -q _flag_ignore_status_show_untracked_files
            set daemon_args $daemon_args --ignore-status-show-untracked-files
        end
        if set -q _flag_ignore_bash_show_untracked_files
            set daemon_args $daemon_args --ignore-bash-show-untracked-files
        end
        if set -q _flag_ignore_bash_show_dirty_state
            set daemon_args $daemon_args --ignore-bash-show-dirty-state
        end

        if test -n "$log_level"
            set -g GITSTATUS_DAEMON_LOG (mktemp "$TMPDIR/gitstatus.$fish_pid.log.XXXXXXXXXX")
            or return
            if test "$log_level" = INFO
                set daemon_args $daemon_args --log-level="$log_level"
            end
        else
            set -g GITSTATUS_DAEMON_LOG /dev/null
        end

        string match -qr '[a-zA-Z0-9=-]*' -- "$daemon_args"; or return

        set -g req_fifo (mktemp -u "$TMPDIR/gitstatus.$fish_pid.pipe.req.XXXXXXXXXX"); or return
        set -g resp_fifo (mktemp -u "$TMPDIR/gitstatus.$fish_pid.pipe.resp.XXXXXXXXXX"); or return
        set -g GITSTATUS_SOCK (mktemp -u "$TMPDIR/gitstatus.$fish_pid.sock.XXXXXXXXXX"); or return
        mkfifo "$req_fifo" "$resp_fifo"; or return

        set -lx _gitstatus_daemon "$daemon"
        fish -c (echo 'trap "kill %1 2>/dev/null" SIGINT SIGTERM EXIT'
                 echo 'status job-control full'
                 echo '$_gitstatus_daemon' $daemon_args '<&3 >&4 &'
                 echo 'wait %1'
                 echo 'if test "$status" -ne 0 -a "$status" -ne 10 -a "$status" -le 128' \
                 '    -a -f $_gitstatus_daemon-static'
                 echo '    $_gitstatus_daemon-static' $daemon_args '<&3 >&4 &'
                 echo '    wait %1'
                 echo 'end'
                 echo 'echo -n bye\x1f0\x1e >&4') \
            > "$GITSTATUS_DAEMON_LOG" 2>&1 3< "$req_fifo" 4> "$resp_fifo" &
        set -g GITSTATUS_DAEMON_PID (jobs -lp)

        # Fish currently doesn't support setting redirection for the current shell,
        # which can be done in bash by running `exec` only with redirection.
        # (See the issue https://github.com/fish-shell/fish-shell/issues/3948)
        # As walkaround, we use netcat to redirect fifo to a unix socket.
        begin nc -lkU "$GITSTATUS_SOCK" & end > "$req_fifo" < "$resp_fifo" 2> /dev/null
        set -g GITSTATUS_NC_PID (jobs -lp)

        disown $GITSTATUS_DAEMON_PID $GITSTATUS_NC_PID
        command rm "$req_fifo" "$resp_fifo"; or return

        # Fish currently doesn't support `read` with timeout, use netcat's timeout instead.
        # (See the issue https://github.com/fish-shell/fish-shell/issues/1134)
        set -l resp_fifo (mktemp -u "$TMPDIR/gitstatus.$fish_pid.pipe.tmp.XXXXXXXXXX"); or return
        mkfifo "$resp_fifo"; or return
        set -l resp (fish -c 'echo -n hello\x1f\x1e; expect -c "expect \x1e"' < "$resp_fifo" |
                     nc -U "$GITSTATUS_SOCK" -w "$_flag_timeout" 2> /dev/null | tee "$resp_fifo")
        command rm "$resp_fifo"; or return
        test "$resp" = hello\x1f0\x1e; or return

        set -g GITSTATUS_DIRTY_MAX_INDEX_SIZE "$_flag_max_dirty"
    end

    if not gitstatus_start_impl
        echo 'gitstatus_start: failed to start gitstatusd' >&2
        functions -e gitstatus_start_impl
        gitstatus_stop
        return 1
    end

    functions -e gitstatus_start_impl
    return 0
end

function _gitstatus_start_usage
    echo 'Usage: gitstatus_start [-eUWDh] [-t FLOAT] [-s INT] [-u INT] [-c INT] [-d INT] [-m INT] [--help]'
end

function _gitstatus_start_help
    _gitstatus_start_usage
    echo
    echo '  -t FLOAT  Fail the self-check on initialization if not getting a response from'
    echo '            gitstatusd for this this many seconds. Defaults to 5.'
    echo
    echo '  -s INT    Report at most this many staged changes; negative value means infinity.'
    echo '            Defaults to 1.'
    echo
    echo '  -u INT    Report at most this many unstaged changes; negative value means infinity.'
    echo '            Defaults to 1.'
    echo
    echo '  -c INT    Report at most this many conflicted changes; negative value means infinity.'
    echo '            Defaults to 1.'
    echo
    echo '  -d INT    Report at most this many untracked files; negative value means infinity.'
    echo '            Defaults to 1.'
    echo
    echo '  -m INT    Report -1 unstaged, untracked and conflicted if there are more than this many'
    echo '            files in the index. Negative value means infinity. Defaults to -1.'
    echo
    echo '  -e        Count files within untracked directories like `git status --untracked-files`.'
    echo
    echo '  -U        Unless this option is specified, report zero untracked files for repositories'
    echo '            with status.showUntrackedFiles = false.'
    echo
    echo '  -W        Unless this option is specified, report zero untracked files for repositories'
    echo '            with bash.showUntrackedFiles = false.'
    echo
    echo '  -D        Unless this option is specified, report zero staged, unstaged and conflicted'
    echo '            changes for repositories with bash.showDirtyState = false.'
end
