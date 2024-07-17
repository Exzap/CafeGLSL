#!/usr/bin/env bash
meson build-host/ -Db_sanitize=none -Dtools=glsl -Dvulkan-drivers= -Dgallium-drivers=r600 -Db_lundef=false -Dglx=disabled -Degl=disabled -Dplatforms= -Dllvm=disabled --buildtype=release
ninja -C ./build-host
