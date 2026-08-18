# cmake/cpm-config.cmake
# Third-party dependencies resolved through CPM.cmake.
#
# Each package has a wrapper in cmake/cpm/ named <pkg>-config.cmake.
# With CPM_USE_LOCAL_PACKAGES=ON, CPM routes find_package() calls
# through those wrappers, so dependency sources and build options live
# in one place.

find_package(doctest CONFIG REQUIRED)
find_package(gsl CONFIG REQUIRED)

if(WEMOD_ENHANCER_BUILD_GUI)
    # imgui-config.cmake creates the imgui::sdl3_renderer backend target
    # only when SDL3::SDL3 already exists, so resolve SDL3 first.
    find_package(sdl3 CONFIG REQUIRED)
    find_package(imgui CONFIG REQUIRED)
endif()
