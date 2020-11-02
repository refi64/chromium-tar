#!/bin/bash
cp update-chromium.sh /tmp
git rm -rf .
cp /tmp/update-chromium.sh .

version=86.0.4240.111
curl -Lo chromium.tar.xz https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz
tar -xvf chromium.tar.xz --strip-components=1
sed -i '/angle\|ffmpeg/d' third_party/.gitignore
