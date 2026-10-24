FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build debhelper dpkg-dev fakeroot \
    qt6-base-dev qt6-svg-dev qt6-qpa-plugins qt6-translations-l10n \
    pandoc graphviz libhunspell-1.7-0 libsecret-tools poppler-utils \
    fonts-dejavu-core fonts-noto-cjk \
    python3 xvfb xdotool xauth desktop-file-utils lintian ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
