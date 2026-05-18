#!/bin/bash
./build.sh
sudo rpm-ostree remove blossom-kcm-software-update
sudo rpm-ostree install rpmbuild/RPMS/x86_64/blossom-kcm-software-update-0.1.0-1.x86_64.rpm
sudo rpm-ostree apply-live --allow-replacement
