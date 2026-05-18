BUILD_DIR ?= build
BUILD_TYPE ?= RelWithDebInfo
PREFIX ?= $(HOME)/.local
GENERATOR ?= Ninja

.PHONY: all configure build install test clean appimage

all: build

configure:
	cmake -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" \
		-DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" \
		-DCMAKE_INSTALL_PREFIX="$(PREFIX)" \
		-DSPEECHER_BUILD_TESTS=ON

build: configure
	cmake --build "$(BUILD_DIR)"

install: configure
	cmake --build "$(BUILD_DIR)"
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

test: build
	ctest --test-dir "$(BUILD_DIR)" --output-on-failure

appimage:
	packaging/build-appimage.sh

clean:
	cmake --build "$(BUILD_DIR)" --target clean
