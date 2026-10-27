FROM debian:13

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates build-essential cmake ninja-build \
    qt6-base-dev qt6-wayland qt6-tools-dev qt6-l10n-tools libx11-dev libsecret-1-dev \
    xvfb xauth dbus-x11 desktop-file-utils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake --preset linux-release -DCMAKE_PREFIX_PATH=/usr \
    && cmake --build --preset linux-release --parallel 2 \
    && cmake --install cmake-build-linux-release --prefix /opt/xlibrary \
    && desktop-file-validate \
       /opt/xlibrary/share/applications/io.github.LapplandJulia.XLibrary.desktop

ENV QT_QPA_PLATFORM=xcb

CMD ["xvfb-run", "-a", "dbus-run-session", "--", "/opt/xlibrary/bin/XLibrary", "--check-startup"]
