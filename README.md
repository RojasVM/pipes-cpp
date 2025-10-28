
# pipes-cpp

Remake of the classic animated terminal pipelines in modern C++. Cross‑platform (Linux/Windows) with a simple core and terminal backends.

## Build

### Linux (Arch/CachyOS, Ubuntu, etc.)
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/pipes
```

### Windows (MSVC + CMake + Ninja recommended)
```bat
cmake -S . -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build\pipes.exe
```

## Usage
Run the executable. Press `q` to quit. Other keys depend on the in-app menu/options.

## Notes
- Unified build uses the original `pipes.cpp` with cross‑platform `#ifdef`s.
- Original variants kept in `legacy/original/`.
