#=====================================================================
# CMakeLists.txt for Vermouth
#=====================================================================
cmake_minimum_required(VERSION 3.20)

#------------------------------------------
# Project setting
#------------------------------------------
project(vermouth LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

#------------------------------------------
# Dependencies
#------------------------------------------
find_package(FluidSynth CONFIG REQUIRED)

#------------------------------------------
# Source
#------------------------------------------
file(GLOB_RECURSE SRC_CPP RELATIVE ${PROJECT_SOURCE_DIR} ./src/Win9x/*.cpp)
file(GLOB_RECURSE SRC_C RELATIVE ${PROJECT_SOURCE_DIR} ./src/*.c)
set(SRC_RC ./src/Win9x/vermouth.rc)
list(REMOVE_ITEM SRC_CPP "./src/Win9x/dd2.cpp")

#------------------------------------------
# Make DLL file
#------------------------------------------
add_library(vermouth SHARED ${SRC_C} ${SRC_CPP} ${SRC_RC} ./src/Win9x/vermouth.def )

#------------------------------------------
# Compile Option
#------------------------------------------
target_compile_options(vermouth PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:
        $<$<CONFIG:Release>:/MT /W3 /EHsc>
        $<$<CONFIG:Debug>:/MDd /W3 /EHsc>
    >
)
target_compile_options(vermouth PRIVATE /utf-8)
target_include_directories(vermouth PRIVATE ./src/ ./src/codecnv ./src/common ./src/generic ./src/vermouth ./src/Win9x ./src/zlib)

target_compile_definitions(vermouth PRIVATE
    UNICODE
    _UNICODE
    _USRDLL
# Build for Win7
#    WINVER=0x0601
    WINVER=0x0A00
    $<$<CXX_COMPILER_ID:MSVC>:
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    >
)

#------------------------------------------
# Link
#------------------------------------------
target_link_libraries(vermouth PRIVATE
    user32
    gdi32
    shlwapi
    winmm
    FluidSynth::libfluidsynth
)

#------------------------------------------
# Output folder
#------------------------------------------
set_target_properties(vermouth PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    OUTPUT_NAME vermouth
)

