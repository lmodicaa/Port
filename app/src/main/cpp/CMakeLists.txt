cmake_minimum_required(VERSION 3.22.1)
project("manhunt")

set(CMAKE_CXX_STANDARD 17)

add_library(manhunt SHARED
        native-lib.cpp
        txd_loader.cpp
        dff_loader.cpp
        bsp_loader.cpp
        inst_loader.cpp
        ifp_loader.cpp)

find_library(log-lib log)
find_library(android-lib android)
find_library(gles3-lib GLESv3)
find_library(egl-lib EGL)

target_link_libraries(manhunt
        ${log-lib}
        ${android-lib}
        ${gles3-lib}
        ${egl-lib})