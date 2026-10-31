cmake_minimum_required(VERSION 3.28)

project(cardgame C)

if (MSVC)
        add_compile_options(/utf-8)
endif()

set(
  CMAKE_RUNTIME_OUTPUT_DIRECTORY
  "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
)

set(
  CMAKE_LIBRARY_OUTPUT_DIRECTORY
  "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
)

include(FetchContent)
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE ALWAYS)

function(include_dependency libName gitURL gitTag minVersion)
        FetchContent_Declare(${libName}
                GIT_REPOSITORY ${gitURL}
                GIT_TAG        ${gitTag}
                GIT_SHALLOW    TRUE
                GIT_PROGRESS   TRUE
                FIND_PACKAGE_ARGS ${minVersion}
        )
        FetchContent_MakeAvailable(${libName})
endfunction()

set(SDL_X11_XSCRNSAVER OFF)
include_dependency(SDL3 https://github.com/libsdl-org/SDL.git release-3.4.18 3.4.0)

include_dependency(SDL3_ttf https://github.com/libsdl-org/SDL_ttf.git release-3.2.2 3.2.0)

set(SDLIMAGE_SVG ON)
foreach(format ANI AVIF BMP GIF JPG JXL LBM PCX PNG PNM QOI TGA TIF WEBP XCF XPM XV)
        set(SDLIMAGE_${format} OFF)
endforeach()
set(SDLIMAGE_BACKEND_STB OFF)
set(SDLIMAGE_BACKEND_WIC OFF)
set(SDLIMAGE_BACKEND_IMAGEIO OFF)
set(SDLIMAGE_SAMPLES OFF)
set(SDLIMAGE_INSTALL OFF)
include_dependency(SDL3_image https://github.com/libsdl-org/SDL_image.git release-3.4.8 3.4.0)

set(CSourceFiles "src/app.c" "src/gui.c" "src/cards.c" "src/card_texture.c" "src/card_motion.c" "src/solitari.c" "src/setimig.c" "src/table.c" "src/mona.c")

set(TARGET_NAME ${PROJECT_NAME})
if (ANDROID)
        add_library(main SHARED ${CSourceFiles})
        set(TARGET_NAME main)
else()
        add_executable(${TARGET_NAME} ${CSourceFiles})
endif()

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(${TARGET_NAME} PROPERTIES
        C_STANDARD 17
        C_STANDARD_REQUIRED ON
        C_EXTENSIONS OFF
)
if (MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /W4)
else()
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/resources ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        VERBATIM
)

find_library(C_MATH_LIBRARY m)
if(C_MATH_LIBRARY)
    target_link_libraries(${TARGET_NAME} PUBLIC ${C_MATH_LIBRARY})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
        SDL3::SDL3
        SDL3_ttf::SDL3_ttf
        SDL3_image::SDL3_image
)
