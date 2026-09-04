FROM debian:trixie

ARG USER_ID=1000

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        adwaita-qt6 \
        breeze \
        breeze-icon-theme \
        ca-certificates \
        cmake \
        curl \
        at-spi2-core \
        dbus-x11 \
        file \
        g++ \
        git \
        kwin-wayland \
        libcap2-bin \
        libatspi2.0-dev \
        libdbus-1-dev \
        libegl1 \
        libgl1-mesa-dev \
        libglib2.0-dev \
        libkf6colorscheme-dev \
        libkf6config-dev \
        libkf6globalaccel-dev \
        libkf6widgetsaddons-dev \
        liblayershellqtinterface-dev \
        libsecret-1-dev \
        libwayland-dev \
        libxkbcommon-dev \
        libxkbcommon-x11-0 \
        libxcb-cursor0 \
        ninja-build \
        patchelf \
        plasma-integration \
        pulseaudio \
        python3 \
        qt6-base-dev \
        qt6-declarative-dev \
        qt6-gtk-platformtheme \
        qt6-multimedia-dev \
        qt6-style-kvantum \
        qt6-svg-plugins \
        qt6-wayland \
        qt6-websockets-dev \
        qt6-xdgdesktopportal-platformtheme \
        wayland-protocols \
        wl-clipboard \
    && setcap -r /usr/bin/kwin_wayland \
    && curl --fail --location --retry 3 \
        --output /tmp/qtkeychain.tar.gz \
        "https://github.com/frankosterfeld/qtkeychain/archive/refs/tags/0.15.0.tar.gz" \
    && echo "f4254dc8f0933b06d90672d683eab08ef770acd8336e44dfa030ce041dc2ca22  /tmp/qtkeychain.tar.gz" | sha256sum --check \
    && tar --extract --gzip --file /tmp/qtkeychain.tar.gz --directory /tmp \
    && cmake -S /tmp/qtkeychain-0.15.0 -B /tmp/qtkeychain-build -G Ninja \
        -DBUILD_TRANSLATIONS=OFF \
        -DBUILD_WITH_QT6=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
    && cmake --build /tmp/qtkeychain-build \
    && cmake --install /tmp/qtkeychain-build \
    && rm -rf /tmp/qtkeychain-build /tmp/qtkeychain-0.15.0 /tmp/qtkeychain.tar.gz \
    && curl --fail --location --retry 3 \
        --output /tmp/appimagetool.AppImage \
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage" \
    && echo "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  /tmp/appimagetool.AppImage" | sha256sum --check \
    && install -m 755 /tmp/appimagetool.AppImage /usr/local/bin/appimagetool \
    && rm /tmp/appimagetool.AppImage \
    && ldconfig \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid "$USER_ID" builder \
    && useradd --create-home --uid "$USER_ID" --gid "$USER_ID" builder

ENV APPIMAGE_EXTRACT_AND_RUN=1 \
    LANG=C.UTF-8
USER builder
WORKDIR /workspace
