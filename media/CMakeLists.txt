# vsp_media -- everything that needs to draw or decode.
#
# Kept out of vsp_core on purpose: the core stays free of Qt Gui so it can be
# tested headless and reused unchanged, and the models ask for previews through
# an interface rather than knowing how one is made.

add_library(vsp_media STATIC
    Thumbnailer.h
    Thumbnailer.cpp
)

target_include_directories(vsp_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vsp_media PUBLIC vsp_core Qt6::Gui)

if(TARGET Qt6::Multimedia)
    # Optional: without it, videos get a drawn plate instead of a real frame.
    target_link_libraries(vsp_media PUBLIC Qt6::Multimedia)
    target_compile_definitions(vsp_media PRIVATE VSP_HAVE_MULTIMEDIA)
    message(STATUS "vsp_media: video frame previews enabled")
else()
    message(STATUS "vsp_media: Qt Multimedia not found, videos will use a drawn plate")
endif()

if(MSVC)
    target_compile_options(vsp_media PRIVATE /W4)
else()
    target_compile_options(vsp_media PRIVATE -Wall -Wextra -Wpedantic)
endif()
