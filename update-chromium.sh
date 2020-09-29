#!/bin/bash
version=85.0.4183.121
curl -Lo chromium.tar.xz https://commondatastorage.googleapis.com/chromium-browser-official/chromium-$version.tar.xz
tar -xvf chromium.tar.xz --strip-components=1
rm chromium.tar.xz
