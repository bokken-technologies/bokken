# bokken-cli — unified command-line tool
#
# Builds a single executable that dispatches to the subcommands:
#   build    — full pipeline: tsc → bytecode → pack → cmake
#   watch    — incremental rebuild + live reload on file changes
#   compile  — JS → QuickJS bytecode (.script), incremental
#   pack     — directory tree → .assetpack, incremental
#   clean    — remove build artifacts and caches
#   doctor   — verify the toolchain is present
#   new      — scaffold a new project from the template repo
#
# This CMakeLists is consumed by the engine's top-level CMake via
# add_subdirectory(tools/bokken-cli). It depends on the qjs and miniz
# targets that the engine's FetchContent has already made available;
# building this directory standalone (without the parent) will fail
# at link time, which is intentional — the CLI ships with the engine.

add_executable(bokken-cli
    main.cpp
    Cli.cpp
    Compile.cpp
    Manifest.cpp
    Pack.cpp
    Watch.cpp
    WatchCommand.cpp
    DevelopmentClient.cpp
    GameProcess.cpp
    Project.cpp
    Commands.cpp
    New.cpp
)

# The watcher runs on its own thread and the development client uses the
# platform sockets API. Threads are needed everywhere; Winsock only on
# Windows. The Linux inotify and macOS FSEvents backends need no extra
# link libraries beyond the C library and CoreServices respectively.
find_package(Threads REQUIRED)
target_link_libraries(bokken-cli PRIVATE
    qjs       # script bytecode compilation (Compile.cpp)
    miniz     # zip archive writer (Pack.cpp)
    Threads::Threads
)
if(WIN32)
    target_link_libraries(bokken-cli PRIVATE ws2_32)
elseif(APPLE)
    target_link_libraries(bokken-cli PRIVATE "-framework CoreServices")
endif()

# The QuickJS and miniz headers don't propagate include paths through
# their target interfaces in every release — be explicit so this
# subdirectory builds regardless of upstream CMake hygiene. quickjs-ng
# ships its public quickjs.h under include/ in current releases and at
# the source root in older ones, so list both candidates.
target_include_directories(bokken-cli PRIVATE
    "${quickjs_ng_SOURCE_DIR}"
    "${quickjs_ng_SOURCE_DIR}/include"
    "${miniz_SOURCE_DIR}"
    "${miniz_BINARY_DIR}"  # miniz generates miniz_export.h here
)

# Manifest.cpp reads and writes the incremental build cache as JSON via
# the same nlohmann/json the engine uses. Prefer the imported target's
# interface include if FetchContent exposed one; fall back to the source
# dir's single-include path otherwise.
if(TARGET nlohmann_json::nlohmann_json)
    target_link_libraries(bokken-cli PRIVATE nlohmann_json::nlohmann_json)
elseif(DEFINED nlohmann_json_SOURCE_DIR)
    target_include_directories(bokken-cli PRIVATE
        "${nlohmann_json_SOURCE_DIR}/single_include")
endif()

# New.cpp uses std::regex and shells out to `git`. No extra link deps
# needed for the regex (libstdc++) but the implementation does need
# C++20 for std::filesystem; the parent CMake already enforces that.
