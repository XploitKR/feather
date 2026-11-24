#!/bin/sh
# Rebuilds FDSceneFix.dylib and regenerates Feather/Utilities/Handlers/FDSceneFix.swift.
# Works with Command Line Tools only (stub .tbd files stand in for the iOS SDK).
set -e
cd "$(dirname "$0")"
clang -target arm64-apple-ios16.6 -dynamiclib -fblocks -O2 -nostdlib -Wno-incompatible-sysroot \
  -install_name @executable_path/Frameworks/FDSceneFix.dylib -o FDSceneFix.dylib \
  fdscene.c stubs/libSystem.tbd stubs/libobjc.tbd
python3 -I gen_swift.py FDSceneFix.dylib ../../Feather/Utilities/Handlers/FDSceneFix.swift
