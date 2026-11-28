@echo off
rem Build D3d7Lib.dll (32-bit) on Windows with LLVM (https://github.com/llvm/llvm-project/releases)
rem and Python 3 on PATH. No Visual Studio or Windows SDK needed.
setlocal
cd /d "%~dp0"
if not exist build mkdir build
python src\gen_stubs.py build || goto :fail
llvm-dlltool -m i386 -k -d src\imports\kernel32.def -l build\kernel32.lib || goto :fail
llvm-dlltool -m i386 -k -d src\imports\user32.def   -l build\user32.lib || goto :fail
clang -target i686-pc-windows-msvc -O2 -ffreestanding -fno-builtin -fno-stack-protector -mno-stack-arg-probe -mno-sse -mno-mmx -march=i686 -Wall -Wno-misleading-indentation -Ibuild -Isrc -c src\soulbringer_modern.c -o build\soulbringer_modern.obj || goto :fail
lld-link /dll /nodefaultlib /entry:DllMain /machine:x86 /safeseh:no /dynamicbase /subsystem:windows /def:build\soulbringer_modern.def /out:build\D3d7Lib.dll build\soulbringer_modern.obj build\kernel32.lib build\user32.lib || goto :fail
echo built build\D3d7Lib.dll
exit /b 0
:fail
echo BUILD FAILED
exit /b 1
