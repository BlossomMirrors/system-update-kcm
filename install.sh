#!/bin/bash
cmake -B build -S . \\
    -GNinja \\
    -DCMAKE_BUILD_TYPE=Release \\
    -DCMAKE_INSTALL_PREFIX=/usr
ninja -C build
sudo rpm-ostree usroverlay
sudo ninja -C build install
