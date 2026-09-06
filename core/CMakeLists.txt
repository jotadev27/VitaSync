# vsp_core -- every byte of transport, parsing and queueing logic lives here and
# is shared verbatim between the desktop and Android front ends.

find_package(ZLIB REQUIRED)

add_library(vsp_core STATIC
    vsp/PathUtils.h            vsp/PathUtils.cpp
    vsp/SfoReader.h            vsp/SfoReader.cpp
    vsp/ZipReader.h            vsp/ZipReader.cpp
    vsp/PackageInspector.h     vsp/PackageInspector.cpp
    vsp/ThemeReader.h          vsp/ThemeReader.cpp
    vsp/ArchiveExtractor.h     vsp/ArchiveExtractor.cpp
    vsp/RemoteTreeScanner.h    vsp/RemoteTreeScanner.cpp
    vsp/VitaPaths.h            vsp/VitaPaths.cpp
    vsp/MetadataDb.h           vsp/MetadataDb.cpp
    vsp/RemoteBrowserModel.h   vsp/RemoteBrowserModel.cpp
    vsp/DropStageModel.h       vsp/DropStageModel.cpp
    vsp/ThumbnailProvider.h
    vsp/RemoteTransport.h
    vsp/FtpClient.h            vsp/FtpClient.cpp
    vsp/UsbTransport.h         vsp/UsbTransport.cpp
    vsp/CompanionClient.h      vsp/CompanionClient.cpp
    vsp/TransferQueue.h        vsp/TransferQueue.cpp
    vsp/VitaDevice.h           vsp/VitaDevice.cpp
    vsp/Version.h
)

target_include_directories(vsp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vsp_core PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent PRIVATE ZLIB::ZLIB)
target_compile_definitions(vsp_core PUBLIC VSP_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(vsp_core PRIVATE /W4)
else()
    target_compile_options(vsp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
