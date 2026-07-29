cpmaddpackage(
    NAME
    implot
    VERSION
    1.0
    GITHUB_REPOSITORY
    epezent/implot
    SYSTEM
    ON
    EXCLUDE_FROM_ALL
    ON
    GIT_SHALLOW
    ON
    DOWNLOAD_ONLY
    TRUE)

add_library(implot STATIC)

target_sources(
    implot
    PRIVATE ${implot_SOURCE_DIR}/implot.cpp
            ${implot_SOURCE_DIR}/implot_items.cpp
            ${implot_SOURCE_DIR}/implot_demo.cpp # optional demo
)

target_include_directories(implot SYSTEM PUBLIC ${implot_SOURCE_DIR})

target_link_libraries(implot PRIVATE imgui)
