## GUI

Prefer clicking over typing? `wemod_enhancer_gui` is a small Dear ImGui window (stock SDL3 + SDL_Renderer backend) that runs the same tested `wemod_enhancer.py patch` / `restore` commands and streams their output live into a scrolling log. Pick the WeMod folder (auto-detected when possible, with a Browse... dialog), press Patch - a "Copy output" button grabs the full log for error reports, and the python command / script path / version.dll path live under a collapsed "Advanced" section. SDL3, the C++ runtime and the CRT are linked statically where the toolchain ships the static archives - the binary runs on a bare OS install; Python 3.11+ is the only requirement.

The GUI is opt-in (`-DWEMOD_ENHANCER_BUILD_GUI=ON`, already set in the presets below); the plain DLL build never touches SDL3/imgui:

```sh
cmake --workflow --preset msvc-full              # Windows: build/msvc/gui/Release/wemod_enhancer_gui.exe
cmake --workflow --preset llvm-mingw-x86_64-full # on Linux: Windows .exe (cross) + version.dll
cmake --workflow --preset linux-native-full      # on Linux: build/linux-native/gui/Release/wemod_enhancer_gui (ELF)
```

Linux host builds need Wayland/X11 development packages for SDL3, e.g. `sudo apt-get install libwayland-dev libxkbcommon-dev libdbus-1-dev libibus-1.0-dev libdecor-0-dev`. Build-time only - at runtime SDL3 loads the display libraries dynamically, and `cmake --install` marks the ELF executable automatically. For a statically linked C++ runtime on Fedora, also `sudo dnf install libstdc++-static` (Debian/Ubuntu ship it in `libstdc++-*-dev`); without it the ELF falls back to the system libstdc++, which every base install has.
