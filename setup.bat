git submodule update --init --recursive
cmake . --preset default
cmake --build ./build --config Release
start ./build/src/Release/Sunaba.exe