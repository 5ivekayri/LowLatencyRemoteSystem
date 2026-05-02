cmake_minimum_required(VERSION 3.22)

project(LowLatencyRemoteSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(remote_core STATIC
    src/capture/DxgiAdapterEnumerator.cpp
    src/capture/DxgiDuplicator.cpp
    src/decode/MfH264Decoder.cpp
    src/encode/MfH264Encoder.cpp
    src/input/ClientInputCapture.cpp
    src/input/InputInjector.cpp
    src/render/D3D11Renderer.cpp
    src/transport/TcpControlChannel.cpp
    src/transport/UdpVideoTransport.cpp
)

target_include_directories(remote_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(remote_core PUBLIC
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(remote_core PRIVATE
        /W4
        /permissive-
    )
endif()

target_link_libraries(remote_core PUBLIC
    d3d11
    dxgi
    mfplat
    mf
    mfuuid
    evr
    wmcodecdspuuid
    ws2_32
    user32
    gdi32
    ole32
)

add_executable(remote_host
    apps/host/main.cpp
    apps/host/HostApp.cpp
)

target_link_libraries(remote_host PRIVATE remote_core)

add_executable(remote_client
    apps/client/main.cpp
    apps/client/ClientApp.cpp
)

target_link_libraries(remote_client PRIVATE remote_core)
