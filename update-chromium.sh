#!/bin/bash
version=86.0.4240.75
curl -Lo chromium.tar.xz https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz
tar -xvf chromium.tar.xz --strip-components=1
rm chromium.tar.xz
