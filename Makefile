BINARY_NAME = BokkenEngine
BUILD_DIR = build
WINDOWS_BUILD_DIR = build_windows
TOOLCHAIN_FILE = mingw-w64.cmake

NINJA_FOUND := $(shell command -v ninja 2> /dev/null)

ifdef NINJA_FOUND
    GENERATOR = -G Ninja
else
    GENERATOR = 
endif

ifdef VIRTUAL_ENV
    PYTHON_HINT = -DPython3_EXECUTABLE=$(VIRTUAL_ENV)/bin/python3
else
    PYENV_PYTHON := $(shell command -v pyenv >/dev/null 2>&1 && pyenv which python3 2>/dev/null)
    ifneq ($(PYENV_PYTHON),)
        PYTHON_HINT = -DPython3_EXECUTABLE=$(PYENV_PYTHON)
    else
        PYTHON_HINT =
    endif
endif

# OS Detection and Extension Setup
ifeq ($(OS),Windows_NT)
    LIB_EXT = .dll
    LIB_PREFIX = 
    P_SEP = \\
    CLEAN_CMD = rmdir /s /q $(BUILD_DIR) $(WINDOWS_BUILD_DIR) 2>nul || exit 0
    TOOLCHAIN_PATH = $(TOOLCHAIN_FILE)
else
    DETECTED_OS = $(shell uname -s)
    P_SEP = /
    CLEAN_CMD = rm -rf $(BUILD_DIR) $(WINDOWS_BUILD_DIR)
    TOOLCHAIN_PATH = $(shell pwd)/$(TOOLCHAIN_FILE)
    ifeq ($(DETECTED_OS),Darwin)
        LIB_EXT = .dylib
        LIB_PREFIX = lib
    else
        LIB_EXT = .so
        LIB_PREFIX = lib
    endif
endif

all: build

setup:
	cmake -S . -B $(BUILD_DIR) $(GENERATOR) $(PYTHON_HINT) -DCMAKE_BUILD_TYPE=Debug

build: setup
	cmake --build $(BUILD_DIR)

# Run shows you where the library is
run: build
	@echo "Target: $(LIB_PREFIX)$(BINARY_NAME)$(LIB_EXT)"
	@ls -lh $(BUILD_DIR)$(P_SEP)bin$(P_SEP)$(LIB_PREFIX)$(BINARY_NAME)*$(LIB_EXT) || true

windows:
	@cmake -E make_directory $(WINDOWS_BUILD_DIR)
	cmake -S . -B $(WINDOWS_BUILD_DIR) \
		$(GENERATOR) \
		$(PYTHON_HINT) \
		-DCMAKE_TOOLCHAIN_FILE=$(TOOLCHAIN_PATH) \
		-DCMAKE_BUILD_TYPE=Release
	cmake --build $(WINDOWS_BUILD_DIR)

# Print the command to install Jinja2 into the Python that CMake actually
# picked. glad's code generator imports Jinja2 at build time; if you saw
# `ModuleNotFoundError: No module named 'jinja2'`, run `make jinja2`
# to get the exact pip command for the right interpreter, then paste it
# into your shell.
#
# We deliberately print rather than execute — installing into the user's
# Python is a side effect that should be opt-in, not silent.
jinja2:
	@if [ ! -f $(BUILD_DIR)/CMakeCache.txt ]; then \
		echo "No build/ yet — run 'make setup' first, then 'make jinja2'."; \
		exit 1; \
	fi
	@PY=$$(grep '^Python3_EXECUTABLE:' $(BUILD_DIR)/CMakeCache.txt | cut -d= -f2); \
	if [ -z "$$PY" ]; then \
		echo "Couldn't find Python3_EXECUTABLE in $(BUILD_DIR)/CMakeCache.txt."; \
		echo "Has CMake configured glad yet? Try 'make setup' first."; \
		exit 1; \
	fi; \
	echo "CMake picked: $$PY"; \
	echo ""; \
	echo "Run this to install Jinja2 into it:"; \
	echo "  $$PY -m pip install --break-system-packages jinja2"

clean:
	$(CLEAN_CMD)

.PHONY: all setup build run windows clean jinja2