# syntax=docker/dockerfile:1

ARG UBUNTU_VERSION=22.04

FROM ubuntu:${UBUNTU_VERSION} AS build

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        bison \
        build-essential \
        flex \
        libexpat1-dev \
        ninja-build \
        pkg-config \
        python3-mako \
        python3-packaging \
        python3-pip \
        python3-setuptools \
        python3-yaml \
        zlib1g-dev \
    && pip3 install --no-cache-dir 'meson>=1.4' \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN LDFLAGS='-static-libgcc -static-libstdc++' meson setup build \
        -Db_sanitize=none \
        -Dtools=glsl \
        -Dvulkan-drivers= \
        -Dgallium-drivers=r600 \
        -Db_lundef=false \
        -Dglx=disabled \
        -Degl=disabled \
        -Dplatforms= \
        -Dllvm=disabled \
        --buildtype=release \
    && ninja -C build \
        cafecompiler/glslcompiler \
        cafecompiler/cafeglsl-smoke \
    && meson test -C build --no-rebuild --print-errorlogs \
        cafeglsl-smoke \
        cafeglsl-skinning-regression \
        cafeglsl-uber-regression \
        cafeglsl-single-pixel-block \
        cafeglsl-pair-auto \
        cafeglsl-pair-block-reversed \
        cafeglsl-pair-default \
        cafeglsl-single-vertex-block \
        cafeglsl-single-pixel-auto \
        cafeglsl-pair-register-rejects-block \
        cafeglsl-invalid-uniform-policy \
    && install -Dm755 build/cafecompiler/glslcompiler /out/glslcompiler \
    && strip --strip-unneeded /out/glslcompiler

FROM ubuntu:${UBUNTU_VERSION} AS runtime

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        zlib1g \
    && rm -rf /var/lib/apt/lists/*

LABEL org.opencontainers.image.title="CafeGLSL" \
      org.opencontainers.image.description="GLSL to GX2 shader compiler for the Wii U" \
      org.opencontainers.image.source="https://github.com/Exzap/CafeGLSL"

COPY --from=build /out/glslcompiler /usr/local/bin/glslcompiler
COPY --from=build /src/docs/license.rst /usr/share/doc/cafeglsl/MESA-LICENSE.rst
COPY --from=build /src/licenses /usr/share/licenses/cafeglsl

WORKDIR /work
ENTRYPOINT ["glslcompiler"]
