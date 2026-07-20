#!/bin/bash
./build.sh
sudo rpm-ostree usroverlay
sudo ninja -C build install
