#!/bin/bash
set -xe

cp update-chromium.sh /tmp
git rm -rf . ||:
cp /tmp/update-chromium.sh .

version=87.0.4280.66
curl -Lo chromium.tar.xz https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz
tar -xvf chromium.tar.xz --strip-components=1
sed -i '/angle\|ffmpeg/d' third_party/.gitignore
