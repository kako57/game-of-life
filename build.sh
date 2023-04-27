set -x
# if you have mingw already, just change the name of the compiler to g++.exe?

# compiling with mingw-w64 g++, with highest optimization level
x86_64-w64-mingw32-g++ main.cpp -Wall -O3 -lgdi32 -o main.exe

# for clang-cl.exe users, here's how you can compile it... maybe need user32.lib as well? idk
# clang-cl -fdiagnostics-absolute-paths -Zi ./main.cpp gdi32.lib

# clang gang
# clang -fdiagnostics-absolute-paths main.cpp -o main.exe -lgdi32
