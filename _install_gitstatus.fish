# Copyright 2019 Roman Perepelitsa.
#
# This file is part of GitStatus. It's the fisher install script.
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

if set -q pkg
    set -U GITSTATUS_DIR "$pkg"
    source "$pkg/gitstatus_start.fish"
    source "$pkg/gitstatus_stop.fish"
end

if set -q target
    command rm -f "$target" # remove itself
end
