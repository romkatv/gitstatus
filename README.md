# gitstatus
**gitstatus** enables fast vcs/git prompt in interactive shells. It works on Mac OS, Linux, FreeBSD
and WSL.

[Powerlevel10k](https://github.com/romkatv/powerlevel10k) ZSH theme users don't need to do anything
manually to take advantage of gitstatus because the plugin is bundled with the theme. Other theme
develpers and shell prompt enthusiasts are welcome to integrate with gitstatus.

## How it works

When using common tools such as
[vcs_info](http://zsh.sourceforge.net/Doc/Release/User-Contributions.html#vcs_005finfo-Quickstart)
to embed git status in shell prompt, latency is high for three main reasons:

  1. About a dozen processes are created to generate each prompt. Creating processes and pipes to
     communicate with them is expensive.
  2. There is a lot of redundancy between different `git` commands that are invoked. For example,
     every command has to scan parent directories in search of `.git`. Many commands have to
     resolve HEAD. Several have to read index.
  3. There is redundancy between consecutive prompts, too. Even if you stay within the same repo,
     each prompt will read git index form disk.

gitstatus solves these problems by running a daemon next to each interactive shell. Whenever prompt
needs to refresh, it sends the current directory to the daemon and receives git info back, all with
a single roundtrip. The daemon is written in C++ and is using a
[patched version of libgit2](https://github.com/romkatv/libgit2.git) optimized for efficiency. It
keeps indices of all repos in memory for faster access and does a lot of crazy things to give you
the status of your git repo as fast as possible. It never serves stale data -- every prompt receives
accurate representation of the current state of the repo.

## Requirements

*  To compile: C++14 compiler, GNU make, cmake.
*  To run: GNU libc on Linux, FreeBSD and WSL; nothing on Mac OS.

## Compiling

There are prebuilt `gitstatusd` binaries in
[bin](https://github.com/romkatv/gitstatus/tree/master/bin). When you source `gitstatus.plugin.zsh`,
it'll pick the right binary for your architecture automatically.

If precompiled binaries don't work for you, you'll need to get your hands dirty.

```zsh
zsh -c "$(curl -fsSL https://raw.githubusercontent.com/romkatv/gitstatus/master/build.zsh)"
```

If everything goes well, the path to your newly built binary will be printed at the end.

If something breaks due to a missing dependency (e.g., `cmake` not found), install the dependency,
remove `/tmp/gitstatus` and retry.

## More Docs

Run `gitstatusd --help` for help or read the same thing in
[options.cc](https://github.com/romkatv/gitstatus/blob/master/src/options.cc).

There are also docs in
[gitstatus.plugin.zsh](https://github.com/romkatv/gitstatus/blob/master/gitstatus.plugin.zsh).

## License

GNU General Public License v3.0. See
[LICENSE](https://github.com/romkatv/gitstatus/blob/master/LICENSE).
