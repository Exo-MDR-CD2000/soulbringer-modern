#!/bin/sh
# Build D3d7Lib.dll (32-bit) with LLVM: clang, lld-link, llvm-dlltool, python3 (or $PYTHON).
# Works on Linux, macOS and Windows (Git Bash / MSYS2). No Windows SDK needed.
set -e
cd "$(dirname "$0")"
mkdir -p build
"${PYTHON:-python3}" src/gen_stubs.py build
llvm-dlltool -m i386 -k -d src/imports/kernel32.def -l build/kernel32.lib
llvm-dlltool -m i386 -k -d src/imports/user32.def   -l build/user32.lib
clang -target i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe \
      -mno-sse -mno-mmx -march=i686 -Wall -Wno-misleading-indentation \
      -Ibuild -Isrc -c src/soulbringer_modern.c -o build/soulbringer_modern.obj
# lld-link options written as -opt (same as /opt) so Git Bash does not rewrite them as paths
lld-link -dll -nodefaultlib -entry:DllMain -machine:x86 -safeseh:no -dynamicbase \
      -subsystem:windows -def:build/soulbringer_modern.def -out:build/D3d7Lib.dll \
      build/soulbringer_modern.obj build/kernel32.lib build/user32.lib
echo "built build/D3d7Lib.dll"
