FROM debian:trixie

ARG USER_ID=1000

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install --yes --no-install-recommends \
        breeze \
        breeze-icon-theme \
        ca-certificates \
        cmake \
        curl \
        file \
        g++ \
        git \
        libatspi2.0-dev \
        libdbus-1-dev \
        libegl1 \
        libgl1-mesa-dev \
        libglib2.0-dev \
        libkf6colorscheme-dev \
        libkf6globalaccel-dev \
        libkf6iconthemes-dev \
        libkf6widgetsaddons-dev \
        liblayershellqtinterface-dev \
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
        qt6-gtk-platformtheme \
        qt6-multimedia-dev \
        qt6-svg-plugins \
        qt6-wayland \
        qt6-websockets-dev \
        qt6-xdgdesktopportal-platformtheme \
        qtkeychain-qt6-dev \
        wayland-protocols \
        wl-clipboard \
    && curl --fail --location --retry 3 \
        --output /tmp/appimagetool.AppImage \
        "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage" \
    && echo "ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0  /tmp/appimagetool.AppImage" | sha256sum --check \
    && install -m 755 /tmp/appimagetool.AppImage /usr/local/bin/appimagetool \
    && rm /tmp/appimagetool.AppImage \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid "$USER_ID" builder \
    && useradd --create-home --uid "$USER_ID" --gid "$USER_ID" builder

ENV APPIMAGE_EXTRACT_AND_RUN=1 \
    LANG=C.UTF-8
USER builder
WORKDIR /workspace
