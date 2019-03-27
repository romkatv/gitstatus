# gitstatus
**gitstatus** is a fast alternative to `git status`. Its primary use case is to enable fast git
prompt in interactive shells.

![Pure Power ZSH Theme](https://github.com/romkatv/gitstatus/tree/purepower.png)

gitstatus is bundled with several ZSH themes including
[Powerlevel10k](https://github.com/romkatv/powerlevel10k).

## Table of Contents

1. [What it does](#what-it-does)
2. [How it works](#how-it-works)
3. [Benchmarks](#benchmarks)
4. [Requirements](#requirements)
5. [Compiling](#compiling)
6. [User documentation](#user-documentation)
7. [License](#license)

## What it does

gitstatus reads requests from stdin and prints responses to stdout. Request contain an ID and
a directory. Response contains the same ID and machine-readable git status for the directory.
gitstatus keeps some state in memory for the directories it has seen in order to serve future
requests faster.

[ZSH bindings](https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh) start
gitstatus in the background and communicate with it via pipes.
[Powerlevel10k](https://github.com/romkatv/powerlevel10k) uses these bindings to put git status
into `PROMPT`.

## How it works

gitstatus is built on top of [patched libgit2](https://github.com/romkatv/libgit2) with a few
large performance optimizations, a number of small ones, and a score of bug fixes. The biggest
performance wins come from the following sources:

  * Using all available cores to scan index and work directory in parallel.
  * Reducing the number of `stat` calls to the absolute minimum.
  * Avoiding unnecessary string comparisons, especially with long shared prefixes.
  * Parsing and evaluating `.gitignore` rules lazily.

Changes to libgit2 are extensive but the testing they underwent isn't. It is _not recommended_ to
use the patched libgit2 or gitstatus in production.

## Benchmarks

The following benchmark results were obtained on Intel i9-7900X running Ubuntu 18.04 in
a clean [chromium](https://github.com/chromium/chromium) repository synced to `9bcb0ae`. The
repository was checked out to an ext4 filesysem on M.2 SSD.

Three functionally equivalent tools for obtaining git status were benchmarked:

  * gitstatus itself
  * `git status` with untracked cache enabled
  * `lg2 status` -- a subset of `git status` functionality implemented on libgit2 as a demo/example;
    for the purposes of this benchmark the subset is sufficient to generate the same data as the
    other tools

Every tool was benchmark in cold and hot conditions. For `git status` the first run in a repository
was considered cold, with the following runs considered hot. `lg2 status` was patched to compute
status twice in a single invocation without freeing the repository in between; the second run was
considered hot. Similarly with gitstatus -- the same request was sent to it twice, the second being
hot.

| Tool          |      Cold  |       Hot |
|---------------|-----------:| ---------:|
| **gitstatus** | **329 ms** | **73 ms** |
| `git status`  |     876 ms |    663 ms |
| `lg2 status`  |    1731 ms |   1311 ms |

In this benchmark gitstatus is 9 times faster than `git status` and 18 times faster than
`lg2 status` when status for the same repository is requested for the second time. This is the
primary use case for interactive shells.

## Requirements

*  To compile: C++14 compiler, GNU make, cmake.
*  To run: GNU libc on Linux, FreeBSD and WSL; nothing on Mac OS.

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
[build.zsh](https://github.com/romkatv/gitstatus/tree/build.zsh) and improvise.

## User documentation

Run `gitstatusd --help` for help or read the same thing in
[options.cc](https://github.com/romkatv/gitstatus/blob/master/src/options.cc).

ZSH bindings are documented in
[gitstatus.plugin.zsh](https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh).

## License

GNU General Public License v3.0. See
[LICENSE](https://github.com/romkatv/gitstatus/blob/master/LICENSE). Contributions are covered by
the same license.
