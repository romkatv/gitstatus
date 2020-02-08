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

function gitstatus_stop --description='Stop gitstatusd if it is running' --on-event fish_exit
    set -q GITSTATUS_DAEMON_PID; and kill "$GITSTATUS_DAEMON_PID" 2> /dev/null
    set -q GITSTATUS_NC_PID; and kill "$GITSTATUS_NC_PID" 2> /dev/null
    set -q GITSTATUS_SOCK; and command rm -f "$GITSTATUS_SOCK"

    set -e GITSTATUS_DAEMON_PID
    set -e GITSTATUS_NC_PID
    set -e GITSTATUS_SOCK
    set -e GITSTATUS_DIRTY_MAX_INDEX_SIZE

    return 0
end
