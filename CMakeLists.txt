cmake_minimum_required(VERSION 3.16)

if(APPLE)
    project(BokkenEngine LANGUAGES CXX C OBJC)
else()
    project(BokkenEngine LANGUAGES CXX C)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Versioning logic — date-based.
string(TIMESTAMP CURRENT_YEAR  "%Y")
string(TIMESTAMP CURRENT_MONTH "%m")
string(TIMESTAMP CURRENT_DAY   "%d")
set(BOKKEN_VERSION  "${CURRENT_YEAR}.${CURRENT_MONTH}.${CURRENT_DAY}")
set(BOKKEN_SOVERSION "${CURRENT_YEAR}")

message(STATUS "Configuring ${PROJECT_NAME} - Version: ${BOKKEN_VERSION}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# glad's generator is a Python script that imports Jinja2. Rather than
# asking the user to `pip install jinja2` into whatever Python CMake
# happened to find (often Homebrew's, which is externally-managed and
# rejects pip installs without --break-system-packages), we spin up a
# private venv under the build tree and install Jinja2 into it, then
# force glad to use that interpreter.
#
# This block has to run BEFORE FetchContent_MakeAvailable(glad) so the
# cache variables are in place when glad's own find_package(Python ...)
# runs and freezes its choice into the Python::Interpreter target.

# find SOME Python to bootstrap the venv with. We don't care which
# one — it just needs to be able to run `-m venv`. After bootstrap we
# throw this reference away and use the venv exclusively.
find_program(BOKKEN_BOOTSTRAP_PYTHON
    NAMES python3 python
    REQUIRED
)

set(BOKKEN_GLAD_VENV "${CMAKE_BINARY_DIR}/glad-venv")
if(WIN32)
    set(BOKKEN_GLAD_VENV_PYTHON "${BOKKEN_GLAD_VENV}/Scripts/python.exe")
else()
    set(BOKKEN_GLAD_VENV_PYTHON "${BOKKEN_GLAD_VENV}/bin/python3")
endif()

if(NOT EXISTS "${BOKKEN_GLAD_VENV_PYTHON}")
    message(STATUS "Creating glad generator venv at ${BOKKEN_GLAD_VENV}")
    execute_process(
        COMMAND "${BOKKEN_BOOTSTRAP_PYTHON}" -m venv "${BOKKEN_GLAD_VENV}"
        RESULT_VARIABLE BOKKEN_VENV_RESULT
    )
    if(NOT BOKKEN_VENV_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to create venv with ${BOKKEN_BOOTSTRAP_PYTHON}. "
            "Install the venv module (on Debian/Ubuntu: apt install python3-venv) "
            "and reconfigure.")
    endif()
endif()

# Verify Jinja2 is installed in the venv (idempotent — pip is a no-op
# when the package is already at the latest compatible version).
message(STATUS "Ensuring Jinja2 is installed in glad generator venv")
execute_process(
    COMMAND "${BOKKEN_GLAD_VENV_PYTHON}" -m pip install --quiet jinja2
    RESULT_VARIABLE BOKKEN_PIP_RESULT
)
if(NOT BOKKEN_PIP_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to install Jinja2 into ${BOKKEN_GLAD_VENV}.")
endif()

# jam the venv interpreter into every Python-related cache variable
# CMake's FindPython modules look at, BEFORE glad's subproject runs. glad's
# CMakeLists uses find_package(Python ...) (no version suffix), so we have
# to cover Python_EXECUTABLE, Python3_EXECUTABLE, and PYTHON_EXECUTABLE
# (the legacy FindPythonInterp name, which some older bundled scripts
# still check). FIND_STRATEGY=LOCATION tells FindPython to honor our
# explicit path instead of going hunting for "the newest Python on PATH"
# and overruling us. FIND_VIRTUALENV=ONLY says: refuse to look anywhere
# but inside an active virtualenv.
set(Python_EXECUTABLE        "${BOKKEN_GLAD_VENV_PYTHON}" CACHE FILEPATH "" FORCE)
set(Python3_EXECUTABLE       "${BOKKEN_GLAD_VENV_PYTHON}" CACHE FILEPATH "" FORCE)
set(PYTHON_EXECUTABLE        "${BOKKEN_GLAD_VENV_PYTHON}" CACHE FILEPATH "" FORCE)
set(Python_FIND_STRATEGY     LOCATION                     CACHE STRING  "" FORCE)
set(Python3_FIND_STRATEGY    LOCATION                     CACHE STRING  "" FORCE)
set(Python_FIND_VIRTUALENV   ONLY                         CACHE STRING  "" FORCE)
set(Python3_FIND_VIRTUALENV  ONLY                         CACHE STRING  "" FORCE)

# also poison PATH so any subprocess that searches for "python3"
# rather than honoring CMake's cache (looking at you, glad's custom
# commands) finds our venv copy first. The venv's bin/ dir contains a
# python3 symlink pointing at the venv interpreter.
if(WIN32)
    set(ENV{PATH} "${BOKKEN_GLAD_VENV}/Scripts;$ENV{PATH}")
else()
    set(ENV{PATH} "${BOKKEN_GLAD_VENV}/bin:$ENV{PATH}")
endif()

# activate the venv via VIRTUAL_ENV — FindPython's VIRTUALENV=ONLY
# mode reads this env var to locate the active venv.
set(ENV{VIRTUAL_ENV} "${BOKKEN_GLAD_VENV}")

message(STATUS "Glad will use Python: ${BOKKEN_GLAD_VENV_PYTHON}")

# Everything is vendored at configure time and built from source. No system
# packages required — first `cmake -B build` will clone these into
# third_party/ and subsequent builds reuse the cache.
include(FetchContent)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
endif()

# Once populated, don't try to update on every reconfigure. This prevents
# the git stash/unstash conflicts that FetchContent triggers when the
# cached source directory has any local state (timestamps, generated
# files, etc.). Run `cmake -B build -DFETCHCONTENT_UPDATES_DISCONNECTED=OFF`
# or nuke the relevant third_party/<dep>-* directories to force a fresh pull.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)

# SDL3 — windowing, input, GL context, audio device.
FetchContent_Declare(
  SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)

# SDL3_ttf — TTF rasterization (consumed by GlyphCache).
FetchContent_Declare(
  SDL3_ttf
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
# CRITICAL: tell SDL_ttf to build its own FreeType/HarfBuzz so we don't
# depend on system installations of those libs.
set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES  OFF CACHE BOOL "" FORCE)

# SDL3_image — image loading (PNG, JPEG, etc.) for the TextureCache.
FetchContent_Declare(
  SDL3_image
  GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
set(SDLIMAGE_VENDORED ON  CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES  OFF CACHE BOOL "" FORCE)

# QuickJS-NG — scripting runtime.
FetchContent_Declare(
  quickjs_ng
  GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)
set(BUILD_TESTING  OFF CACHE BOOL "" FORCE)
set(QJS_BUILD_LIBC OFF CACHE BOOL "" FORCE)

# nlohmann/json — project.bokken parsing.
FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)

# PhysFS — virtual filesystem for asset packs.
FetchContent_Declare(
  physfs
  GIT_REPOSITORY https://github.com/icculus/physfs.git
  GIT_TAG        main
  GIT_SHALLOW    TRUE
)
set(PHYSFS_BUILD_SHARED      OFF             CACHE BOOL   "" FORCE)
set(PHYSFS_BUILD_STATIC      ON              CACHE BOOL   "" FORCE)
set(PHYSFS_BUILD_TEST        OFF             CACHE BOOL   "" FORCE)
set(PHYSFS_TARGETNAME_STATIC "physfs-static" CACHE STRING "" FORCE)

# miniz — zip/deflate for the asset packer.
FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        2.2.0
  GIT_SHALLOW    TRUE
)
set(MINIZ_BUILD_SHARED OFF CACHE BOOL "" FORCE)

# GLM — math (header-only).
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
  GIT_SHALLOW    TRUE
)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)

# dr_libs — single-header WAV/MP3/FLAC decoders for the audio engine.
FetchContent_Declare(
  dr_libs
  GIT_REPOSITORY https://github.com/mackron/dr_libs.git
  GIT_TAG        master
  GIT_SHALLOW    TRUE
)

# Box2D v3 — 2D rigid-body physics. The 3.x line is a full C rewrite by
# Erin Catto with a sub-stepped solver that handles high mass ratios and
# stacks far better than the old 2.x line, plus event arrays returned
# from b2World_Step instead of contact-listener callbacks (which is what
# Bokken's Physics::World::dispatchEvents reads).
#
# Pinned to v3.1.1 (June 2025) rather than tracking `main`. The `main`
# branch is moving toward v3.2.0 with renamed fields (motionLocks
# replacing fixedRotation, etc.) and our integration is written against
# the documented v3.1.x API. When 3.2.0 ships and stabilises we can
# revisit this pin and update the bindings together.
#
# Build flags: turn off the bundled samples / unit tests / docs since
# we only consume the library, and force a static build to match the
# rest of the vendored deps.
FetchContent_Declare(
  box2d
  GIT_REPOSITORY https://github.com/erincatto/box2d.git
  GIT_TAG        v3.1.1
  GIT_SHALLOW    TRUE
)
set(BOX2D_SAMPLES   OFF CACHE BOOL "" FORCE)
set(BOX2D_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(BOX2D_DOCS      OFF CACHE BOOL "" FORCE)
set(BOX2D_PROFILE   OFF CACHE BOOL "" FORCE)
set(BOX2D_VALIDATE  OFF CACHE BOOL "" FORCE)
set(BOX2D_UNIT_TESTS OFF CACHE BOOL "" FORCE)

# glad — OpenGL loader. Generated source, no runtime DLL/.so dependency.
# We pulled in glad after a previous attempt with GLEW broke on modern
# macOS SDKs (the GLEW CMake build pulls in `-framework AGL` which Apple
# removed from the SDK around macOS 14). glad has no such baggage —
# it ships pure C source, calls our supplied loader (SDL_GL_GetProcAddress)
# at runtime, and links only against the system OpenGL framework.
#
# Configure-time requirement: Python 3 with the venv module. We bootstrap
# Jinja2 into a private venv above so the user doesn't have to install
# anything into their system Python.
FetchContent_Declare(
  glad
  GIT_REPOSITORY https://github.com/Dav1dde/glad.git
  GIT_TAG        v2.0.8
  GIT_SHALLOW    TRUE
  # glad's project-level CMakeLists is in cmake/, not the repo root.
  SOURCE_SUBDIR  cmake
)

# Globally prefer static libs.
set(BUILD_SHARED_LIBS         OFF CACHE BOOL "" FORCE)
set(SDLTTF_BUILD_SHARED_LIBS  OFF CACHE BOOL "" FORCE)
set(SDL3TTF_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(SDL3IMAGE_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# Suppress warnings from third-party code. They're not ours to fix and
# they drown out warnings from our own source files. Flags are restored
# immediately after FetchContent so our engine code still gets full
# diagnostic coverage.
set(BOKKEN_SAVED_C_FLAGS "${CMAKE_C_FLAGS}")
set(BOKKEN_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
set(BOKKEN_SAVED_OBJC_FLAGS "${CMAKE_OBJC_FLAGS}")
string(APPEND CMAKE_C_FLAGS " -w")
string(APPEND CMAKE_CXX_FLAGS " -w")
string(APPEND CMAKE_OBJC_FLAGS " -w")

# Populate everything. First run: clones into third_party/ and configures
# each subproject. Subsequent runs: reuses the cached source.
FetchContent_MakeAvailable(SDL3 SDL3_ttf SDL3_image quickjs_ng json physfs miniz glm dr_libs box2d glad)

set(CMAKE_C_FLAGS "${BOKKEN_SAVED_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${BOKKEN_SAVED_CXX_FLAGS}")
set(CMAKE_OBJC_FLAGS "${BOKKEN_SAVED_OBJC_FLAGS}")

# Generate the actual glad library. The flags:
#   STATIC       — link statically into the engine, same as our other deps.
#   REPRODUCIBLE — pin the generated code to glad's bundled Khronos specs
#                  rather than fetching latest, so all machines/builds
#                  produce byte-identical output.
#   LOADER       — emit `gladLoadGL(loader_fn)` so we can plug in
#                  SDL_GL_GetProcAddress. Without this flag glad only
#                  ships function pointer tables and you have to write
#                  the loader yourself.
#   API gl:core=3.3 — matches the context profile we request in
#                  Renderer::Base::init.
glad_add_library(glad_gl_core_33 STATIC REPRODUCIBLE LOADER API gl:core=3.3)

# dr_libs is header-only — we need exactly one .c file that defines the
# IMPLEMENTATION macro so the symbols (drwav_init, drwav_read_pcm_frames_*,
# etc.) end up in the build. Only WAV is enabled by default; flip on
# DR_MP3_IMPLEMENTATION / DR_FLAC_IMPLEMENTATION here when you need them.
set(DR_LIBS_IMPL_C "${CMAKE_CURRENT_BINARY_DIR}/dr_libs_impl.c")
file(WRITE "${DR_LIBS_IMPL_C}"
    "#define DR_WAV_IMPLEMENTATION\n"
    "#include <dr_wav.h>\n")
add_library(bokken_dr_libs STATIC "${DR_LIBS_IMPL_C}")
target_include_directories(bokken_dr_libs PUBLIC "${dr_libs_SOURCE_DIR}")

# System OpenGL framework — glad's runtime functions resolve through the
# loader we hand to gladLoadGL (SDL_GL_GetProcAddress), but we still link
# the OS-provided GL library so the symbols glad's loader queries actually
# exist at link/runtime. find_package(OpenGL) just resolves the system
# framework — no install required, it ships with the OS.
find_package(OpenGL REQUIRED)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_library(${PROJECT_NAME} SHARED ${SOURCES})

set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION                    ${BOKKEN_VERSION}
    SOVERSION                  ${BOKKEN_SOVERSION}
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    SDL3::SDL3-static
    SDL3_ttf::SDL3_ttf-static
    SDL3_image::SDL3_image-static
    miniz
    qjs
    nlohmann_json::nlohmann_json
    physfs-static
    glm::glm
    glad_gl_core_33     # OpenGL loader (replaces hand-rolled GL surface)
    OpenGL::GL          # System GL — glad's loader resolves into this
    bokken_dr_libs      # audio engine
    box2d               # 2D physics (Box2D v3, vendored above)
)

# Public/private include split.
#
# PUBLIC — propagates to anything that links the engine. Only Bokken.hpp
#          (the slim entry-point header) lives in include/. Consumers see
#          this directory; they do NOT see anything under src/ or any of
#          the third-party trees. That's the whole point.
#
# PRIVATE — only the engine's own .cpp files compile against these. The
#           third-party include dirs (PhysFS, etc.) used to be PUBLIC,
#           which forced every consumer to reproduce them — that's how
#           the example's CMakeLists ended up listing 8 third_party paths.
#           Demoted to PRIVATE now that nothing in include/Bokken.hpp
#           references them.
target_include_directories(${PROJECT_NAME}
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${physfs_SOURCE_DIR}/src"
)

# Platform-specific system libs.
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE shell32 imm32 setupapi version winmm)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl)
endif()

# bokken-cli — unified command-line tool (compile / pack / new).
# Replaces the old separate Packer and Compiler executables. Lives in
# tools/bokken-cli/ as a sibling tool target alongside the engine; its
# own CMakeLists links against the qjs and miniz targets that the
# FetchContent_MakeAvailable call above has already populated.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/bokken-cli/CMakeLists.txt")
    add_subdirectory(tools/bokken-cli)
endif()