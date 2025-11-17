# pipes-cpp

**pipes-cpp** is a modern C++ remake of the classic animated terminal screensaver *“pipes”*.
It is fully **cross-platform (Linux / Windows)**, featuring a lightweight core and terminal-specific backends.

---

## Build

### Linux distros

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

---

## Usage

Run the compiled executable to start the animation.
Press `q` to quit.
Other keys and configuration options are accessible from the in-app menu.

---

## Suggested Configuration

A recommended setup for smooth and visually balanced performance:

```
A/Z  Pipes:            8
S/X  Straight [5..15]: 15
F/D  FPS [20..100]:    100
L/J  Limit chars:      1000
R    Random start:     ON
K    Keep on edge:     ON
C    Color enabled:    ON
V    Vivid colors:     ON
T    Type set:         0 (0..9)
```

You can adjust the number of pipes or frame rate according to your terminal performance.

---

## Notes

* The unified build uses the main source file `pipes.cpp` with platform-specific `#ifdef` directives.
* Original and legacy versions are available under `legacy/original/`.
* Tested with **g++ 14 / Clang 18 / MSVC 2022**.
* Requires **C++17 or newer**.
