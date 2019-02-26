/*
 * libgit2 "status" example - shows how to use the status APIs
 *
 * Written by the libgit2 contributors
 *
 * To the extent possible under law, the author(s) have dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * You should have received a copy of the CC0 Public Domain Dedication along
 * with this software. If not, see
 * <http://creativecommons.org/publicdomain/zero/1.0/>.
 */

#include <stdio.h>
#include <unistd.h>

#include <git2.h>

#include <string>

#include "common.h"
#include "check.h"
#include "logging.h"

/**
 * This example demonstrates the use of the libgit2 status APIs,
 * particularly the `git_status_list` object, to roughly simulate the
 * output of running `git status`.  It serves as a simple example of
 * using those APIs to get basic status information.
 *
 * This does not have:
 *
 * - Robust error handling
 * - Colorized or paginated output formatting
 *
 * This does have:
 *
 * - Examples of translating command line arguments to the status
 *   options settings to mimic `git status` results.
 * - A sample status formatter that matches the default "long" format
 *   from `git status`
 * - A sample status formatter that matches the "short" format
 */

enum {
  FORMAT_DEFAULT = 0,
  FORMAT_LONG = 1,
  FORMAT_SHORT = 2,
  FORMAT_PORCELAIN = 3,
};

#define MAX_PATHSPEC 8

struct opts {
  git_status_options statusopt;
  const char* repodir;
  char* pathspec[MAX_PATHSPEC];
  int npaths;
  int format;
  int zterm;
  int showbranch;
  int showsubmod;
  int repeat;
};

static void parse_opts(struct opts* o, int argc, char* argv[]);
static void show_branch(git_repository* repo, int format);
static void print_long(git_status_list* status);
static void print_short(git_repository* repo, git_status_list* status);
static int print_submod(git_submodule* sm, const char* name, void* payload);

int lg2_status(git_repository* repo, int argc, char* argv[]) {
  git_status_list* status;
  struct opts o = {GIT_STATUS_OPTIONS_INIT, "."};

  o.statusopt.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
  o.statusopt.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX |
                      GIT_STATUS_OPT_SORT_CASE_SENSITIVELY;

  parse_opts(&o, argc, argv);

  if (git_repository_is_bare(repo))
    fatal("Cannot report status on bare repository", git_repository_path(repo));

show_status:
  if (o.repeat) printf("\033[H\033[2J");

  /**
   * Run status on the repository
   *
   * We use `git_status_list_new()` to generate a list of status
   * information which lets us iterate over it at our
   * convenience and extract the data we want to show out of
   * each entry.
   *
   * You can use `git_status_foreach()` or
   * `git_status_foreach_ext()` if you'd prefer to execute a
   * callback for each entry. The latter gives you more control
   * about what results are presented.
   */
  check_lg2(git_status_list_new(&status, repo, &o.statusopt), "Could not get status", nullptr);

  if (o.showbranch) show_branch(repo, o.format);

  if (o.showsubmod) {
    int submod_count = 0;
    check_lg2(git_submodule_foreach(repo, print_submod, &submod_count), "Cannot iterate submodules",
              o.repodir);
  }

  if (o.format == FORMAT_LONG)
    print_long(status);
  else
    print_short(repo, status);

  git_status_list_free(status);

  if (o.repeat) {
    sleep(o.repeat);
    goto show_status;
  }

  return 0;
}

/**
 * If the user asked for the branch, let's show the short name of the
 * branch.
 */
static void show_branch(git_repository* repo, int format) {
  int error = 0;
  const char* branch = nullptr;
  git_reference* head = nullptr;

  error = git_repository_head(&head, repo);

  if (error == GIT_EUNBORNBRANCH || error == GIT_ENOTFOUND)
    branch = nullptr;
  else if (!error) {
    branch = git_reference_shorthand(head);
  } else
    check_lg2(error, "failed to get current branch", nullptr);

  if (format == FORMAT_LONG)
    printf("# On branch %s\n", branch ? branch : "Not currently on any branch.");
  else
    printf("## %s\n", branch ? branch : "HEAD (no branch)");

  git_reference_free(head);
}

/**
 * This function print out an output similar to git's status command
 * in long form, including the command-line hints.
 */
static void print_long(git_status_list* status) {
  size_t i, maxi = git_status_list_entrycount(status);
  const git_status_entry* s;
  int header = 0, changes_in_index = 0;
  int changed_in_workdir = 0, rm_in_workdir = 0;
  const char *old_path, *new_path;

  /** Print index changes. */

  for (i = 0; i < maxi; ++i) {
    const char* istatus = nullptr;

    s = git_status_byindex(status, i);

    if (s->status == GIT_STATUS_CURRENT) continue;

    if (s->status & GIT_STATUS_WT_DELETED) rm_in_workdir = 1;

    if (s->status & GIT_STATUS_INDEX_NEW) istatus = "new file: ";
    if (s->status & GIT_STATUS_INDEX_MODIFIED) istatus = "modified: ";
    if (s->status & GIT_STATUS_INDEX_DELETED) istatus = "deleted:  ";
    if (s->status & GIT_STATUS_INDEX_RENAMED) istatus = "renamed:  ";
    if (s->status & GIT_STATUS_INDEX_TYPECHANGE) istatus = "typechange:";

    if (istatus == nullptr) continue;

    if (!header) {
      printf("# Changes to be committed:\n");
      printf("#   (use \"git reset HEAD <file>...\" to unstage)\n");
      printf("#\n");
      header = 1;
    }

    old_path = s->head_to_index->old_file.path;
    new_path = s->head_to_index->new_file.path;

    if (old_path && new_path && strcmp(old_path, new_path))
      printf("#\t%s  %s -> %s\n", istatus, old_path, new_path);
    else
      printf("#\t%s  %s\n", istatus, old_path ? old_path : new_path);
  }

  if (header) {
    changes_in_index = 1;
    printf("#\n");
  }
  header = 0;

  /** Print workdir changes to tracked files. */

  for (i = 0; i < maxi; ++i) {
    const char* wstatus = nullptr;

    s = git_status_byindex(status, i);

    /**
     * With `GIT_STATUS_OPT_INCLUDE_UNMODIFIED` (not used in this example)
     * `index_to_workdir` may not be `nullptr` even if there are
     * no differences, in which case it will be a `GIT_DELTA_UNMODIFIED`.
     */
    if (s->status == GIT_STATUS_CURRENT || s->index_to_workdir == nullptr) continue;

    /** Print out the output since we know the file has some changes */
    if (s->status & GIT_STATUS_WT_MODIFIED) wstatus = "modified: ";
    if (s->status & GIT_STATUS_WT_DELETED) wstatus = "deleted:  ";
    if (s->status & GIT_STATUS_WT_RENAMED) wstatus = "renamed:  ";
    if (s->status & GIT_STATUS_WT_TYPECHANGE) wstatus = "typechange:";

    if (wstatus == nullptr) continue;

    if (!header) {
      printf("# Changes not staged for commit:\n");
      printf("#   (use \"git add%s <file>...\" to update what will be committed)\n",
             rm_in_workdir ? "/rm" : "");
      printf("#   (use \"git checkout -- <file>...\" to discard changes in working directory)\n");
      printf("#\n");
      header = 1;
    }

    old_path = s->index_to_workdir->old_file.path;
    new_path = s->index_to_workdir->new_file.path;

    if (old_path && new_path && strcmp(old_path, new_path))
      printf("#\t%s  %s -> %s\n", wstatus, old_path, new_path);
    else
      printf("#\t%s  %s\n", wstatus, old_path ? old_path : new_path);
  }

  if (header) {
    changed_in_workdir = 1;
    printf("#\n");
  }

  /** Print untracked files. */

  header = 0;

  for (i = 0; i < maxi; ++i) {
    s = git_status_byindex(status, i);

    if (s->status == GIT_STATUS_WT_NEW) {
      if (!header) {
        printf("# Untracked files:\n");
        printf("#   (use \"git add <file>...\" to include in what will be committed)\n");
        printf("#\n");
        header = 1;
      }

      printf("#\t%s\n", s->index_to_workdir->old_file.path);
    }
  }

  header = 0;

  /** Print ignored files. */

  for (i = 0; i < maxi; ++i) {
    s = git_status_byindex(status, i);

    if (s->status == GIT_STATUS_IGNORED) {
      if (!header) {
        printf("# Ignored files:\n");
        printf("#   (use \"git add -f <file>...\" to include in what will be committed)\n");
        printf("#\n");
        header = 1;
      }

      printf("#\t%s\n", s->index_to_workdir->old_file.path);
    }
  }

  if (!changes_in_index && changed_in_workdir)
    printf("no changes added to commit (use \"git add\" and/or \"git commit -a\")\n");
}

/**
 * This version of the output prefixes each path with two status
 * columns and shows submodule status information.
 */
static void print_short(git_repository* repo, git_status_list* status) {
  size_t i, maxi = git_status_list_entrycount(status);
  const git_status_entry* s;
  char istatus, wstatus;
  const char *extra, *a, *b, *c;

  for (i = 0; i < maxi; ++i) {
    s = git_status_byindex(status, i);

    if (s->status == GIT_STATUS_CURRENT) continue;

    a = b = c = nullptr;
    istatus = wstatus = ' ';
    extra = "";

    if (s->status & GIT_STATUS_INDEX_NEW) istatus = 'A';
    if (s->status & GIT_STATUS_INDEX_MODIFIED) istatus = 'M';
    if (s->status & GIT_STATUS_INDEX_DELETED) istatus = 'D';
    if (s->status & GIT_STATUS_INDEX_RENAMED) istatus = 'R';
    if (s->status & GIT_STATUS_INDEX_TYPECHANGE) istatus = 'T';

    if (s->status & GIT_STATUS_WT_NEW) {
      if (istatus == ' ') istatus = '?';
      wstatus = '?';
    }
    if (s->status & GIT_STATUS_WT_MODIFIED) wstatus = 'M';
    if (s->status & GIT_STATUS_WT_DELETED) wstatus = 'D';
    if (s->status & GIT_STATUS_WT_RENAMED) wstatus = 'R';
    if (s->status & GIT_STATUS_WT_TYPECHANGE) wstatus = 'T';

    if (s->status & GIT_STATUS_IGNORED) {
      istatus = '!';
      wstatus = '!';
    }

    if (istatus == '?' && wstatus == '?') continue;

    /**
     * A commit in a tree is how submodules are stored, so
     * let's go take a look at its status.
     */
    if (s->index_to_workdir && s->index_to_workdir->new_file.mode == GIT_FILEMODE_COMMIT) {
      unsigned int smstatus = 0;

      if (!git_submodule_status(&smstatus, repo, s->index_to_workdir->new_file.path,
                                GIT_SUBMODULE_IGNORE_UNSPECIFIED)) {
        if (smstatus & GIT_SUBMODULE_STATUS_WD_MODIFIED)
          extra = " (new commits)";
        else if (smstatus & GIT_SUBMODULE_STATUS_WD_INDEX_MODIFIED)
          extra = " (modified content)";
        else if (smstatus & GIT_SUBMODULE_STATUS_WD_WD_MODIFIED)
          extra = " (modified content)";
        else if (smstatus & GIT_SUBMODULE_STATUS_WD_UNTRACKED)
          extra = " (untracked content)";
      }
    }

    /**
     * Now that we have all the information, format the output.
     */

    if (s->head_to_index) {
      a = s->head_to_index->old_file.path;
      b = s->head_to_index->new_file.path;
    }
    if (s->index_to_workdir) {
      if (!a) a = s->index_to_workdir->old_file.path;
      if (!b) b = s->index_to_workdir->old_file.path;
      c = s->index_to_workdir->new_file.path;
    }

    if (istatus == 'R') {
      if (wstatus == 'R')
        printf("%c%c %s %s %s%s\n", istatus, wstatus, a, b, c, extra);
      else
        printf("%c%c %s %s%s\n", istatus, wstatus, a, b, extra);
    } else {
      if (wstatus == 'R')
        printf("%c%c %s %s%s\n", istatus, wstatus, a, c, extra);
      else
        printf("%c%c %s%s\n", istatus, wstatus, a, extra);
    }
  }

  for (i = 0; i < maxi; ++i) {
    s = git_status_byindex(status, i);

    if (s->status == GIT_STATUS_WT_NEW) printf("?? %s\n", s->index_to_workdir->old_file.path);
  }
}

static int print_submod(git_submodule* sm, const char* name, void* payload) {
  int* count = (int*)payload;
  (void)name;

  if (*count == 0) printf("# Submodules\n");
  (*count)++;

  printf("# - submodule '%s' at %s\n", git_submodule_name(sm), git_submodule_path(sm));

  return 0;
}

/**
 * Parse options that git's status command supports.
 */
static void parse_opts(struct opts* o, int argc, char* argv[]) {
  struct args_info args = ARGS_INFO_INIT;

  for (args.pos = 1; args.pos < argc; ++args.pos) {
    char* a = argv[args.pos];

    if (a[0] != '-') {
      if (o->npaths < MAX_PATHSPEC)
        o->pathspec[o->npaths++] = a;
      else
        fatal("Example only supports a limited pathspec", nullptr);
    } else if (!strcmp(a, "-s") || !strcmp(a, "--short"))
      o->format = FORMAT_SHORT;
    else if (!strcmp(a, "--long"))
      o->format = FORMAT_LONG;
    else if (!strcmp(a, "--porcelain"))
      o->format = FORMAT_PORCELAIN;
    else if (!strcmp(a, "-b") || !strcmp(a, "--branch"))
      o->showbranch = 1;
    else if (!strcmp(a, "-z")) {
      o->zterm = 1;
      if (o->format == FORMAT_DEFAULT) o->format = FORMAT_PORCELAIN;
    } else if (!strcmp(a, "--ignored"))
      o->statusopt.flags |= GIT_STATUS_OPT_INCLUDE_IGNORED;
    else if (!strcmp(a, "-uno") || !strcmp(a, "--untracked-files=no"))
      o->statusopt.flags &= ~GIT_STATUS_OPT_INCLUDE_UNTRACKED;
    else if (!strcmp(a, "-unormal") || !strcmp(a, "--untracked-files=normal"))
      o->statusopt.flags |= GIT_STATUS_OPT_INCLUDE_UNTRACKED;
    else if (!strcmp(a, "-uall") || !strcmp(a, "--untracked-files=all"))
      o->statusopt.flags |=
          GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    else if (!strcmp(a, "--ignore-submodules=all"))
      o->statusopt.flags |= GIT_STATUS_OPT_EXCLUDE_SUBMODULES;
    else if (!strncmp(a, "--git-dir=", strlen("--git-dir=")))
      o->repodir = a + strlen("--git-dir=");
    else if (!strcmp(a, "--repeat"))
      o->repeat = 10;
    else if (match_int_arg(&o->repeat, &args, "--repeat", 0))
      /* okay */;
    else if (!strcmp(a, "--list-submodules"))
      o->showsubmod = 1;
    else
      check_lg2(-1, "Unsupported option", a);
  }

  if (o->format == FORMAT_DEFAULT) o->format = FORMAT_LONG;
  if (o->format == FORMAT_LONG) o->showbranch = 1;
  if (o->npaths > 0) {
    o->statusopt.pathspec.strings = o->pathspec;
    o->statusopt.pathspec.count = o->npaths;
  }
}

/* This part is not strictly libgit2-dependent, but you can use this
 * as a starting point for a git-like tool */

typedef int (*git_command_fn)(git_repository*, int, char**);

struct {
  const char* name;
  git_command_fn fn;
  char requires_repo;
} commands[] = {
    // { "add",          lg2_add,          1 },
    // { "blame",        lg2_blame,        1 },
    // { "cat-file",     lg2_cat_file,     1 },
    // { "checkout",     lg2_checkout,     1 },
    // { "clone",        lg2_clone,        0 },
    // { "describe",     lg2_describe,     1 },
    // { "diff",         lg2_diff,         1 },
    // { "fetch",        lg2_fetch,        1 },
    // { "for-each-ref", lg2_for_each_ref, 1 },
    // { "general",      lg2_general,      0 },
    // { "index-pack",   lg2_index_pack,   1 },
    // { "init",         lg2_init,         0 },
    // { "log",          lg2_log,          1 },
    // { "ls-files",     lg2_ls_files,     1 },
    // { "ls-remote",    lg2_ls_remote,    1 },
    // { "merge",        lg2_merge,        1 },
    // { "remote",       lg2_remote,       1 },
    // { "rev-list",     lg2_rev_list,     1 },
    // { "rev-parse",    lg2_rev_parse,    1 },
    // { "show-index",   lg2_show_index,   0 },
    {"status", lg2_status, 1},
    // { "tag",          lg2_tag,          1 },
};

static int run_command(git_command_fn fn, git_repository* repo, struct args_info args) {
  int error;

  /* Run the command. If something goes wrong, print the error message to stderr */
  error = fn(repo, args.argc - args.pos, &args.argv[args.pos]);
  if (error < 0) {
    if (git_error_last() == nullptr)
      fprintf(stderr, "Error without message");
    else
      fprintf(stderr, "Bad news:\n %s\n", git_error_last()->message);
  }

  return !!error;
}

static int usage(const char* prog) {
  size_t i;

  fprintf(stderr, "usage: %s <cmd>...\n\nAvailable commands:\n\n", prog);
  for (i = 0; i < ARRAY_SIZE(commands); i++) fprintf(stderr, "\t%s\n", commands[i].name);

  exit(EXIT_FAILURE);
}

static int lg2_main(int argc, char** argv) {
  struct args_info args = ARGS_INFO_INIT;
  git_repository* repo = nullptr;
  const char* git_dir = nullptr;
  int return_code = 1;
  size_t i;

  if (argc < 2) usage(argv[0]);

  git_libgit2_init();

  for (args.pos = 1; args.pos < args.argc; ++args.pos) {
    char* a = args.argv[args.pos];

    if (a[0] != '-') {
      /* non-arg */
      break;
    } else if (optional_str_arg(&git_dir, &args, "--git-dir", ".git")) {
      continue;
    } else if (!strcmp(a, "--")) {
      /* arg separator */
      break;
    }
  }

  if (args.pos == args.argc) usage(argv[0]);

  if (!git_dir) git_dir = ".";

  for (i = 0; i < ARRAY_SIZE(commands); ++i) {
    if (strcmp(args.argv[args.pos], commands[i].name)) continue;

    /*
     * Before running the actual command, create an instance
     * of the local repository and pass it to the function.
     * */
    if (commands[i].requires_repo) {
      check_lg2(git_repository_open_ext(&repo, git_dir, 0, nullptr), "Unable to open repository '%s'",
                git_dir);
    }

    return_code = run_command(commands[i].fn, repo, args);
    goto shutdown;
  }

  fprintf(stderr, "Command not found: %s\n", argv[1]);

shutdown:
  git_repository_free(repo);
  git_libgit2_shutdown();

  return return_code;
}

namespace {

const char* GitError() {
   const git_error* err = git_error_last();
   return err && err->message ? err->message : "unknown error";
}

std::string RemoteBranchName(git_reference* local) {
  if (!local) return "";
  git_reference* remote = nullptr;
  int error = git_branch_upstream(&remote, local);
  if (error == 0) {
    const char* name = git_reference_shorthand(remote);
    const char* sep = strchr(name, '/');
    CHECK(sep) << "invalid remote branch name: " << name;
    std::string res = sep + 1;
    git_reference_free(remote);
    return res;
  } else {
    CHECK(error == GIT_ENOTFOUND) << GitError();
    return "";
  }
}

}  // namespace

int main() {
  static_cast<void>(&lg2_main);

  git_libgit2_init();
  git_repository* repo = nullptr;
  if (git_repository_open_ext(&repo, ".", GIT_REPOSITORY_OPEN_FROM_ENV, nullptr)) return 1;
  if (git_repository_is_bare(repo)) return 1;

  git_reference* head = nullptr;
  switch (git_repository_head(&head, repo)) {
    case 0:
      puts(git_reference_shorthand(head));
      break;
    case GIT_ENOTFOUND:
      puts("(no head)");
      break;
    case GIT_EUNBORNBRANCH:
      puts("HEAD (no branch)");
      break;
    default:
      LOG(FATAL) << GitError();
  }
  puts(RemoteBranchName(head).c_str());

  git_object *obj = NULL;
  CHECK(git_revparse_single(&obj, repo, "HEAD^{tree}") == 0) << GitError();
  git_tree *tree = NULL;
  CHECK(git_tree_lookup(&tree, repo, git_object_id(obj)) == 0) << GitError();
  git_diff *diff = NULL;
  CHECK(git_diff_tree_to_index(&diff, repo, tree, NULL, NULL) == 0) << GitError();
  printf("%ld\n", git_diff_num_deltas(diff));  // the number of staged files
}
