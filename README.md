# gitstatus
**gitstatus** is a lightweight tool for querying the status of git repos for the use in interactive tools.

The primary motivation is to be able to use git status in shell prompt without suffering terrible latency that all other solutions impose.

When using common tools such as [vcs_info](http://zsh.sourceforge.net/Doc/Release/User-Contributions.html#vcs_005finfo-Quickstart) to embed git status in shell prompt, latency is high for two main reasons:

  1. About a dozen processes are created to generate a prompt. Creating processes and pipes to communicate with them is expensive.
  2. There is a lot of redundancy between different `git` commands that get called. For example, every command has to scan parent directories in search of `.git`. Many commands have to resolve HEAD. Some have to read index.

**gitstatus** solves this problem by assembling all information that the prompt needs in a single process and presenting it in an easy-to-parse format. It's using [libgit2](https://libgit2.org/) under the hood for heavy lifting.

## Requirements

*  To compile: C++17 compiler, GNU make, libgit2.
*  To run: Linux, GNU libc.

## Compiling

For best results, compile libgit2 statically with all optional features disabled and all required feature bundled.

```shell
git clone https://github.com/libgit2/libgit2.git
cd libgit2
mkdir build
cd build
cmake                        \
  -DUSE_SSH=OFF              \
  -DUSE_HTTPS=OFF            \
  -DTHREADSAFE=OFF           \
  -DBUILD_SHARED_LIBS=OFF    \
  -DUSE_EXT_HTTP_PARSER=OFF  \
  -DUSE_BUNDLED_ZLIB=ON      \
  -DSHA1_BACKEND=Generic     \
  -DCMAKE_BUILD_TYPE=Release \
  ..
make VERBOSE=1 -j 20
sudo make install
```

Then build gitstatus itself.

```shell
git clone git@github.com:romkatv/gitstatus.git
cd gitstatus
make
```

## Prebuilt Binaries

If you don't feel like building from sources, you can grab a statically built ELF binary for x64 with a dynamic dependency on glibc 6 from [Releases](https://github.com/romkatv/gitstatus/releases).

## Using Manually

Run `gitstatus --help` for help or read the same thing in [gitstatus.cc](https://github.com/romkatv/gitstatus/blob/master/src/gitstatus.cc).

## Using with Powerlevel9k

If you are using the awesome [Powerlevel9k](https://github.com/bhilburn/powerlevel9k) ZSH theme, you can configure the existing `vcs` prompt to use gitstatus for a massive reduction in latency.

First, use [this fork](https://github.com/romkatv/powerlevel9k/tree/caching) of Powerlevel9k instead of the official release. This fork also enables caching, which speeds up prompt rendering by over 10x. Note that you need to use branch `caching`.

```shell
# Assuming oh-my-zsh at the standard location.
rm -rf ~/.oh-my-zsh/custom/themes/powerlevel9k
git clone -b caching git@github.com:romkatv/powerlevel9k.git ~/.oh-my-zsh/custom/themes/powerlevel9k
```

Then set the following configuration options in your `.zshrc` before sourcing Powerlevel9k.

```
# Enable caching of parts of the prompt to make rendering much faster.
POWERLEVEL9K_USE_CACHE=true

# Enable alternative implementation for the vcs prompt. It's much faster but it only supports git.
# Tell it to not scan for dirty files in repos with over 1k files.
POWERLEVEL9K_VCS_STATUS_COMMAND="$HOME/bin/gitstatus --dirty-max-index-size=1024"
```

Lastly, put `gitstatus` binary in your `~/bin`.

You can check out my [dotfiles-public](https://github.com/romkatv/dotfiles-public) repo for details.

## License

GNU General Public License v3.0. See [LICENSE](https://github.com/romkatv/gitstatus/blob/master/LICENSE).
