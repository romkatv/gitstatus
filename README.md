# gitstatus

**gitstatus** is a 10x faster alternative to `git status` and `git describe`. Its primary use
case is to enable fast git prompt in interactive shells.

![Pure Power ZSH Theme](https://raw.githubusercontent.com/romkatv/gitstatus/master/docs/purepower.png)

gitstatus is bundled with several ZSH themes including
[Powerlevel10k](https://github.com/romkatv/powerlevel10k).

Heavy lifting is done by **gitstatusd** -- a custom binary written in C++.

## Table of Contents

1. [What it does](#what-it-does)
2. [Benchmarks](#benchmarks)
3. [Why fast](#why-fast)
4. [Requirements](#requirements)
5. [Compiling](#compiling)
6. [User documentation](#user-documentation)
7. [License](#license)

## What it does

gitstatusd reads requests from stdin and prints responses to stdout. Requests contain an ID and
a directory. Responses contain the same ID and machine-readable git status for the directory.
gitstatusd keeps some state in memory for the directories it has seen in order to serve future
requests faster.

[ZSH bindings](https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh) start
gitstatusd in the background and communicate with it via pipes.
[Powerlevel10k](https://github.com/romkatv/powerlevel10k) uses these bindings to put git status
in `PROMPT`.

Note that gitstatus cannot be used as a drop-in replacement for `git status` command as it doesn't
produce output in the same format. It does perform the same computation though.

## Benchmarks

The following benchmark results were obtained on Intel i9-7900X running Ubuntu 18.04 in
a clean [chromium](https://github.com/chromium/chromium) repository synced to `9394e49a`. The
repository was checked out to an ext4 filesysem on M.2 SSD.

Three functionally equivalent tools for computing git status were benchmarked:

* `gitstatusd`
* `git` with untracked cache enabled
* `lg2` -- a demo/example executable from [libgit2](https://github.com/romkatv/libgit2) that
  implements a subset of `git` functionality on top of libgit2 API; for the purposes of this
  benchmark the subset is sufficient to generate the same data as the other tools

Every tool was benchmark in cold and hot conditions. For `git` the first run in a repository was
considered cold, with the following runs considered hot. `lg2` was patched to compute results twice
in a single invocation without freeing the repository in between; the second run was considered hot.
The same patching was not done for `git` because `git` cannot be easily modified to refresh inmemory
index state between invocations; in fact, this limitation is one of the primary reasons developers
use libgit2. `gitstatusd` was benchmarked similarly to `lg2` with two result computations in the
same invocation.

Two commands were benchmarked: `status` and `describe`.

### Status

In this benchmark all tools were computing the equivalent of `git status`. Lower numbers are better.

| Tool          |      Cold  |         Hot |
|---------------|-----------:|------------:|
| **gitstatus** | **291 ms** | **30.9 ms** |
| git           |     876 ms |      295 ms |
| lg2           |    1730 ms |     1310 ms |

gitstatusd is substantially faster than the alternatives, especially on hot runs. Note that hot runs
are of primary importance to the main use case of gitstatus in interactive shells.

The performance of `git status` fluctuated wildly in this benchmarks for reasons unknown to the
author. Moreover, performance is sticky -- once `git status` settles around a number, it stays
there for a long time. Numbers as diverse as 295, 352, 663 and 730 had been observed on hot runs on
the same repository. The number in the table is the lowest (fastest or best) that `git status` had
shown.

### Describe

In this benchmark all tools were computing the equivalent of `git describe --tags --exact-match`
to find tags that resolve to the same commit as `HEAD`. Lower numbers are better.

| Tool          |       Cold  |           Hot |
|---------------|------------:|--------------:|
| **gitstatus** | **4.04 ms** | **0.0345 ms** |
| git           |     18.0 ms |       14.5 ms |
| lg2           |      185 ms |       45.2 ms |

gitstatusd is once again faster than the alternatives, more so on hot runs.

## Why fast

Since gitstatusd doesn't have to print all staged/unstaged/untracked files but only report
wheather there are any, it can terminate repository scan early. It can also remember which files
were dirty on the previous run and check them first on the next run to avoid the scan entirely if
the files are still dirty. However, the benchmarks above were performed in a clean repository where
these shortcuts do not trigger. All benchmarked tools had to do the same work -- check the status
of every file in the index to see if it has changed, check every directory for newly created files,
etc. And yet, gitstatusd came ahead by a large margin. This section describes what it does that
makes it so fast.

Most of the following comparisons are done against libgit2 rather than git because of the author's
familiarity with the former but not the with latter. libgit2 has clean, well-documented APIs and an
elegant implementation, which makes it so much easier to work with and to analyze performance
bottlenecks.

### Summary for the impatient

Under the benchmark conditions described above, the equivalent of libgit2's
`git_diff_index_to_workdir` (the most expensive part of `status` command) is 46.3 times faster in
gitstatusd. The speedup comes from the following sources.

* gitstatusd uses more efficient data structures and algorithms and employs performance-conscious
coding style throughout the codebase. This reduces CPU time in userspace by 32x compared to libgit2.
* gitstatusd uses less expensive system calls and makes fewer of them. This reduces CPU time spent
in kernel by 1.9x.
* gitstatusd can utilize multiple cores to scan index and workdir in parallel with almost perfect
scaling. This reduces total run time by 12.4x while having virtually no effect on total CPU time.

### Problem statement

The most resource-intensive part of the `status` command is finding the difference between _index_
and _workdir_ (`git_diff_index_to_workdir` in libgit2). Index is a list of all files in the git
repository with their last modification times. This is an obvious simplification but it suffices for
this exposition. On disk, index is stored sorted by file path. Here's an example of git index:

| File        | Last modification time |
|-------------|-----------------------:|
| Makefile    |   2019-04-01T14:12:32Z |
| src/hello.c |   2019-04-01T14:12:00Z |
| src/hello.h |   2019-04-01T14:12:32Z |

This list needs to be compared to the list of files in the working directory. If any of the files
listed in the index are missing from the workdir or have different last modification time, they are
"unstaged" in gitstatusd parlance. If you run `git status`, they'll be shown as "changes not staged
for commit". Thus, any implementation of `status` command has to call `stat()` or one of its
variants on every file in the index.

In addition, all files in the working directory for which there is no entry in the index at all are
"untracked". `git status` will show them as "untracked files". Finding untracked files requires some
form of work directory traversal.

### Single-threaded scan

Let's see how `git_diff_index_to_workdir` from libgit2 accomplishes these tasks. Here's its CPU
profile from 200 hot runs over chromium repository.

![libgit2 CPU profile (hot)](https://raw.githubusercontent.com/romkatv/gitstatus/master/docs/cpu-profile-libgit2.png)

We can see `__GI__lxstat` taking a lot of time. This is the `stat()` call for every file in the
index. We can also identify `__opendir`, `__readdir` and `__GI___close_nocancel` -- glibc wrappers
for reading the contents of a directory. This is for finding untracked files. Out of the total 232
seconds, 111 seconds -- or 47.7% -- was spent on these calls. The rest is computation -- comparing
strings, sorting arrays, etc.

Now let's take a look at the CPU profile of gitstatusd on the same task.

![gitstatusd CPU profile (hot)](https://raw.githubusercontent.com/romkatv/gitstatus/master/docs/cpu-profile-gitstatusd-hot.png)

The first impression is that this profile looks pruned. This isn't an artifact. The profile was
generated with the same tools and the same flags as the profile of libgit2.

Since both profiles were generated from the same workload, absolute numbers can be compared. We can
see that gitstatusd took 62 seconds in total compared to libgit2's 232 seconds. System calls at the
core of the algorithm are cleary visible. `__GI___fxstatat` is a flavor of `stat()`, and the other
three calls -- `__libc_openat64`, `__libc_close` and `__GI___fxstat` are responsible for opening
directories and finding untracked files. Notice that there is almost nothing else in the profile
apart from these calls. The rest of the code accounts for 3.77 seconds of CPU time -- 32 times less
than in libgit2.

So, one reason gitstatusd is fast is that it has efficient diffing code -- very little time is spent
outside of kernel. However, if we look closely, we can notice that system calls in gitstatusd are
_also_ faster than in libgit2. For example, libgit2 spent 72.07 seconds in `__GI__lxstat` while
gitstatusd spent only 48.82 seconds in `__GI___fxstatat`. There are two reasons for this difference.
First, libgit2 makes more `stat()` calls than is strictly required. It's not necessary to stat
directories because index only has files. There are 25k directories in chromium repository (and 300k
files) -- that's 25k `stat()` calls that could be avoided. The second reason is that libgit2 and
gitstatusd use different flavors of `stat()`. libgit2 uses `lstat()`, which takes a path to the file
as input. Its performance is linear in the number of subdirectories in the path because it needs to
perform a lookup for every one of them and to check permissions. gitstatusd uses `fstatat()`, which
takes a file destriptor to the parent directory and a name of the file. Just a single lookup, less
CPU time.

Similarly to `lstat()` vs `fstatat()`, it's faster to open files and directories with `openat()`
from the parent directory file descriptor than with regular `open()` that accepts full file path.
gitstatusd takes advantage of `openat()` to open directories as fast as possible. It opens about 90%
of the directories (this depends on the actual directory structure of the repository) from the
immediate parent -- the most efficient way -- and the remaining 10% it opens from the repository's
root directory. The reason it's done this way is to keep the maximum number of simultaneously open
file descriptors bounded. libgit2 can have O(repository depth) simultaneously open file descriptors,
which may be OK for a single-threaded application but can baloon to a large number when scans are
done by many threads simultaneously, like in gitstatusd.

There is no equivalent to `__opendir` or `__readdir` in the gitstatusd profile because it uses the
equivalent of [untracked cache](https://git-scm.com/docs/git-update-index#_untracked_cache) from
git. On the first scan of the workdir gitstatusd lists all files just like libgit2. But, unlike
libgit2, it remembers the last modification time of every directory along with the list of
untracked files under it. On the next scan, gitstatusd can skip listing files in directories whose
last modification time hasn't changed.

To summarize, here's what gitstatusd was doing when the CPU profile was captured:

1. `__libc_openat64`: Open every directory for which there are files in the index.
2. `__GI___fxstat`: Check last modification time of the directory. Since it's the same as on the
   last scan, this directory has the same list of untracked files as before, which is empty (the
   repository is clean).
3. `__GI___fxstatat`: Check last modification time for every file in the index that belongs to this
   directory.
4. `__libc_close`: Close the file descriptor to the directory.

Here's how the very first scan of a repository looks like in gitstatusd:

![gitstatusd CPU profile (cold)](https://raw.githubusercontent.com/romkatv/gitstatus/master/docs/cpu-profile-gitstatusd-cold.png)

(Some glibc functions are mislabel on this profile. `explicit_zero` and `__nss_passwd_lookup` are
in reality `strcmp` and `memcmp`.)

This is a superset of the previous -- hot -- profile, with an extra `syscall` and string sorting for
directory listing. gitstatusd uses `getdents64` Linux system call directly, bypassing the glibc
wrapper that libgit2 uses. This is 20% faster, primarily due to the unorthodox sorting algorithm
that can be used together with `getdents64` but not with `readdir`.

### Multithreading

The diffing algorithm in gitstatusd was designed from the ground up with the intention of using it
concurrently from multiple threads. With a fast SSD, `status` is CPU bound, so taking advantage of
all available CPU cores is an obvious way to yield results faster.

gitstatusd exhibits almost perfect scaling from multithreading. Engaging all cores allows it to
produce results 12.4 times faster than in single-threaded execution. This is on Intel i9-7900X with
10 cores (20 with hyperthreading) with single-core frequency of 4.3GHz and all-core frequency of
4.0GHz.

Note: `git status` also uses all available cores in some parts of its algorithm while `lg2` does
everything in a single thread.

### Postprocessing

Once the difference between the index and the workdir is found, we have a list of _candidates_ --
files that may be unstaged or untracked. To make the final judgement, these files need to be checked
against `.gitignore` rules and a few other things.

gitstatusd uses [patched libgit2](https://github.com/romkatv/libgit2) for this step. This fork
adds several optimizations that make libgit2 faster. The patched libgit2 performs more than twice
as fast in the benchmark as the original even without changes in the user code (that is, in the
code that uses the libgit2 APIs). The fork also adds several API extensions, most notable of which
is the support for multi-threaded scans. If `lg2 status` is modified to take advantage of these
extensions, it outperforms the original libgit2 by a factor of 18. Lastly, the fork fixes a score of
bugs, most of which become apparent only when using libgit2 from multiple threads.

_WARNING: Changes to libgit2 are extensive but the testing they underwent isn't. It is
**not recommended** to use the patched libgit2 in production._

## Requirements

* To compile: C++14 compiler, GNU make, cmake.
* To run: GNU libc on Linux, FreeBSD and WSL; nothing on Mac OS.

## Compiling

There are prebuilt `gitstatusd` binaries in
[bin](https://github.com/romkatv/gitstatus/tree/master/bin). When using ZSH bindings privided by
`gitstatus.plugin.zsh`, the right binary for your architecture is picked up automatically.

If precompiled binaries don't work for you, you'll need to get your hands dirty.

```zsh
zsh -c "$(curl -fsSL https://raw.githubusercontent.com/romkatv/gitstatus/master/build.zsh)"
```

If everything goes well, the path to your newly built binary will be printed at the end.

If something breaks due to a missing dependency (e.g., `cmake` not found), install the dependency,
remove `/tmp/gitstatus` and retry.

To build from locally modified sources, read
[build.zsh](https://github.com/romkatv/gitstatus/tree/master/build.zsh) and improvise. This is a
release script from which you'll have to devise a local build script. Expect painful experience if
you aren't familiar with ZSH, C++, GCC, CMake or GNU make.

## User documentation

### gitstatusd

Run `gitstatusd --help` for help or read the same thing in
[options.cc](https://github.com/romkatv/gitstatus/blob/master/src/options.cc).

#### Example

Send a single request and print response (zsh syntax):

```zsh
local req_id=id
local dir=$PWD
echo -nE $req_id$'\x1f'$dir$'\x1e' | ./gitstatusd --num-threads=32 | {
  local resp
  IFS=$'\x1f' read -rd $'\x1e' -A resp && print -lr -- "${(@qq)resp}"
}
```

Output:

```
'id'
'1'
'/home/romka/.oh-my-zsh/custom/plugins/gitstatus'
'6e86ec135bf77875e222463cbac8ef72a7e8d823'
'master'
'master'
'origin'
'git@github.com:romkatv/gitstatus.git'
''
'0'
'1'
'1'
'0'
'0'
'0'
''
```

### ZSH bindings

ZSH bindings are documented in
[gitstatus.plugin.zsh](https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh). There
is support for synchronous and asynchronous requests.

#### Example

Start gitstatusd, send it a request, wait for response and print it.

```zsh
source ./gitstatus.plugin.zsh
gitstatus_start MY
gitstatus_query -d $PWD MY
set | egrep '^VCS_STATUS'
```

Output:

```
VCS_STATUS_ACTION=''
VCS_STATUS_ALL=( /home/romka/.oh-my-zsh/custom/plugins/gitstatus 6e86ec135bf77875e222463cbac8ef72a7e8d823 master master origin git@github.com:romkatv/gitstatus.git '' 0 1 1 0 0 0 '' )
VCS_STATUS_COMMIT=6e86ec135bf77875e222463cbac8ef72a7e8d823
VCS_STATUS_COMMITS_AHEAD=0
VCS_STATUS_COMMITS_BEHIND=0
VCS_STATUS_HAS_STAGED=0
VCS_STATUS_HAS_UNSTAGED=1
VCS_STATUS_HAS_UNTRACKED=1
VCS_STATUS_LOCAL_BRANCH=master
VCS_STATUS_REMOTE_BRANCH=master
VCS_STATUS_REMOTE_NAME=origin
VCS_STATUS_REMOTE_URL=git@github.com:romkatv/gitstatus.git
VCS_STATUS_RESULT=ok-sync
VCS_STATUS_STASHES=0
VCS_STATUS_TAG=''
VCS_STATUS_WORKDIR=/home/romka/.oh-my-zsh/custom/plugins/gitstatus
```

gitstatusd will terminate when you exit zsh from which it was started.

## License

GNU General Public License v3.0. See
[LICENSE](https://github.com/romkatv/gitstatus/blob/master/LICENSE). Contributions are covered by
the same license.
