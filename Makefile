BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
GENERATOR ?= Ninja

ifeq ($(origin PLATFORM), undefined)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
PLATFORM := linux
else ifeq ($(UNAME_S),Darwin)
PLATFORM := macos
else
$(error Unsupported platform "$(UNAME_S)"; pass PLATFORM=linux or PLATFORM=macos)
endif
endif

ifeq ($(filter $(PLATFORM),linux macos),)
$(error Unsupported PLATFORM="$(PLATFORM)"; pass PLATFORM=linux or PLATFORM=macos)
endif

ifeq ($(PLATFORM),macos)
PREFIX ?= /Applications
else
PREFIX ?= $(HOME)/.local
endif

.PHONY: all configure build install test clean appimage dmg

all: build

ifeq ($(PLATFORM),macos)
configure:
	@command -v brew >/dev/null 2>&1 || { echo "Homebrew is required on macOS; install it from https://brew.sh." >&2; exit 1; }
	cmake -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_INSTALL_PREFIX="$(PREFIX)" \
		-DCMAKE_PREFIX_PATH="$$(brew --prefix qt);$$(brew --prefix qtkeychain)" \
		-DSPEECHER_BUILD_TESTS=ON
else
configure:
	cmake -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_INSTALL_PREFIX="$(PREFIX)" \
		-DSPEECHER_BUILD_TESTS=ON
endif

build: configure
	cmake --build "$(BUILD_DIR)"

install: configure
	cmake --build "$(BUILD_DIR)"
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

ifeq ($(PLATFORM),linux)
appimage:
	packaging/build-appimage.sh

dmg:
	$(error dmg is macOS only)
else
appimage:
	$(error appimage is Linux only)

dmg: build
	packaging/macos/build-dmg.sh "$(BUILD_DIR)"
endif

clean:
	cmake --build "$(BUILD_DIR)" --target clean
