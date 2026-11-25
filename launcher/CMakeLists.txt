cmake_minimum_required(VERSION 3.20)
project(seaofstar_tod_launcher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(SeaOfStar-ToD WIN32
    src/main.cpp
    resource.rc
)

target_compile_definitions(SeaOfStar-ToD PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0A00
)

if (MSVC)
    target_compile_options(SeaOfStar-ToD PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(SeaOfStar-ToD PRIVATE gdiplus dwmapi shell32 user32)
