#!/bin/sh

set -e

echo "I: Install dependencies"
sudo apt install -y libglib2.0-dev libboost-filesystem-dev libperl-dev

echo "I: Build Debian Package"
dpkg-buildpackage -uc -us -tc -b
