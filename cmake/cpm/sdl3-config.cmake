# SDL3 package config — find_package(sdl3 CONFIG) lands here via
# CMAKE_PREFIX_PATH (cmake/cpm.cmake). Exposes SDL's own targets:
#   SDL3::SDL3, SDL3::SDL3-shared, SDL3::SDL3-static
# Aliases are created by SDL's CMakeLists — nothing to re-name here.

# find_package re-includes this file per calling scope; run once.
include_guard(GLOBAL)

# --- platform-conditional subsystems: defaults, then overrides ------------
set(sdl_sensor OFF) # mobile-only subsystem
set(sdl_wayland OFF) # Linux desktop integration:
set(sdl_dbus OFF) #   Wayland, D-Bus, IBus, libdecor
set(sdl_ibus OFF)
set(sdl_libdecor OFF)
set(sdl_opengles ON) # Apple uses desktop GL, not ES
set(sdl_shared OFF) # Emscripten has no dynamic linking: static-only
set(sdl_static ON)
set(ct_sdl_render ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(sdl_sensor ON)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(sdl_wayland ON)
    set(sdl_dbus ON)
    set(sdl_ibus ON)
    set(sdl_libdecor ON)
endif()
if(CMAKE_SYSTEM_NAME MATCHES "Darwin|iOS")
    set(sdl_opengles OFF)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(sdl_shared OFF)
    set(sdl_static ON)
endif()

# --- fetch SDL3: windowing, events, OpenGL context creation only ----------
cpmaddpackage(
    NAME
    SDL3
    GITHUB_REPOSITORY
    libsdl-org/SDL
    VERSION
    3.4.14
    GIT_TAG
    release-3.4.14
    GIT_SHALLOW
    ON
    GIT_PROGRESS
    ON
    EXCLUDE_FROM_ALL
    TRUE
    SYSTEM
    TRUE
    OPTIONS
    # ---- build tooling ----
    "SDL_CCACHE ON"
    "SDL_WERROR OFF" # dep warnings must never become errors in our build
    "SDL_PCH OFF" # goes stale on incremental Android Studio builds
    # ---- library type ----
    "SDL_STATIC ${sdl_static}"
    "SDL_SHARED ${sdl_shared}"
    # ---- core subsystems ----
    "SDL_AUDIO OFF"
    "SDL_VIDEO ON"
    "SDL_GPU OFF"
    "SDL_RENDER ${ct_sdl_render}"
    "SDL_CAMERA OFF"
    "SDL_JOYSTICK OFF"
    "SDL_HAPTIC OFF"
    "SDL_HIDAPI OFF"
    "SDL_POWER OFF"
    "SDL_SENSOR ${sdl_sensor}"
    # ---- video backends ----
    "SDL_X11 ON"
    "SDL_WAYLAND ${sdl_wayland}"
    "SDL_KMSDRM OFF"
    "SDL_RPI OFF"
    "SDL_ROCKCHIP OFF"
    "SDL_VIVANTE OFF"
    "SDL_DUMMYVIDEO OFF"
    "SDL_OFFSCREEN OFF"
    "SDL_OPENVR OFF"
    # ---- context APIs ----
    "SDL_OPENGL ON"
    "SDL_OPENGLES ${sdl_opengles}"
    # ---- Linux desktop integration ----
    "SDL_DBUS ${sdl_dbus}"
    "SDL_IBUS ${sdl_ibus}"
    "SDL_WAYLAND_LIBDECOR ${sdl_libdecor}"
    # ---- input / misc ----
    "SDL_LIBUDEV OFF"
    "SDL_HIDAPI_LIBUSB OFF"
    "SDL_HIDAPI_JOYSTICK OFF"
    "SDL_VIRTUAL_JOYSTICK OFF"
    # ---- tests / examples / install ----
    "SDL_TESTS OFF"
    "SDL_TEST_LIBRARY OFF"
    "SDL_EXAMPLES OFF"
    "SDL_INSTALL OFF"
    "SDL_INSTALL_TESTS OFF"
    "SDL_DISABLE_INSTALL_DOCS ON")

# --- Android: ship SDL's Java bindings + base manifest/proguard -----------
# Configure-time copy from the fetched tree — a build-time target raced
# AGP's compile*JavaWithJavac. file(COPY/COPY_FILE) without RESULT fail
# fatally on missing input: no manual existence checks needed.
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
    set(sdl3_gen "${CMAKE_SOURCE_DIR}/android_project/app/build/generated/sdl3")

    file(
        COPY "${SDL3_SOURCE_DIR}/android-project/app/src/main/java/org/"
        DESTINATION "${sdl3_gen}/java/org"
        FILES_MATCHING
        PATTERN "*.java")

    # ONLY_IF_DIFFERENT keeps the timestamp when unchanged: no pointless
    # Gradle manifest merge on re-configure.
    file(
        COPY_FILE
        "${SDL3_SOURCE_DIR}/android-project/app/src/main/AndroidManifest.xml"
        "${sdl3_gen}/AndroidManifest.xml"
        ONLY_IF_DIFFERENT)
    file(
        COPY_FILE
        "${SDL3_SOURCE_DIR}/android-project/app/proguard-rules.pro"
        "${sdl3_gen}/proguard-rules.pro"
        ONLY_IF_DIFFERENT)
endif()
