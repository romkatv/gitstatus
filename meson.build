project(
    'gitstatus',
    'cpp',
    license: 'GPL-3.0-or-later',
    license_files: ['LICENSE'],
    meson_version: '>=1.2.0',
    default_options: {
        'cpp_std': 'c++14',
        'warning_level': '1',
        'buildtype': 'release',
        'b_ndebug': 'if-release',
        'b_lto': true,
        'b_pie': true,
    },
)

keyval = import('keyval')
conf = configuration_data(keyval.load('build.info'))
version = conf.get_unquoted('gitstatus_version')

pcre2_dep = dependency('libpcre2-8', required: false)
cxx = meson.get_compiler('cpp')
have_http_parser = cxx.has_header('http_parser.h')

cmake = import('cmake')
libgit2_opts = cmake.subproject_options()
libgit2_opts.add_cmake_defines(
    {
        'CMAKE_BUILD_TYPE': 'None',
        'BUILD_SHARED_LIBS': false,
        'ENABLE_REPRODUCIBLE_BUILDS': true,
        'BUILD_CLAR': false,
        'THREADSAFE': true,
        'USE_GSSAPI': false,
        'USE_HTTPS': false,
        'USE_NTLMCLIENT': false,
        'USE_SSH': false,
        'ZERO_NSEC': true,
        'USE_HTTP_PARSER': have_http_parser ? 'system' : 'builtin',
        'REGEX_BACKEND': pcre2_dep.found() ? 'pcre2' : 'builtin',
    },
)
libgit2_opts.set_override_option('cpp_std', 'c++20')
libgit2 = cmake.subproject('libgit2', options: libgit2_opts)

gitstatusd = executable(
    'gitstatusd',
    files(
        'src/arena.cc',
        'src/check_dir_mtime.cc',
        'src/dir.cc',
        'src/git.cc',
        'src/gitstatus.cc',
        'src/index.cc',
        'src/logging.cc',
        'src/options.cc',
        'src/repo.cc',
        'src/repo_cache.cc',
        'src/request.cc',
        'src/response.cc',
        'src/strings.cc',
        'src/tag_db.cc',
        'src/thread_pool.cc',
        'src/timer.cc',
    ),
    cpp_args: [
        '-funsigned-char',
        '-DGITSTATUS_VERSION=' + version,
    ],
    dependencies: [
        dependency('threads'),
        libgit2.dependency('git2'),
    ],
    install: true,
)
