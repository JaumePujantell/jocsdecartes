cmake_minimum_required(VERSION 3.28)

project(cardgame C)

if (ANDROID)
        # so much for system independence...
        add_custom_command(
                OUTPUT generate_deck
                COMMAND gcc ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_deck.c -I ${CMAKE_CURRENT_SOURCE_DIR}/src -o generate_deck
                DEPENDS tools/generate_deck.c
                VERBATIM
        )
else ()
        add_executable(generate_deck "tools/generate_deck.c")
        target_include_directories(generate_deck PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

add_custom_command(
  OUTPUT deck.c
  COMMAND ./generate_deck > deck.c
  DEPENDS generate_deck
  VERBATIM
)

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

# define a function for adding git dependencies
function(include_dependency libName gitURL gitTag)
        FetchContent_Declare(${libName}
                GIT_REPOSITORY ${gitURL}
                GIT_TAG        ${gitTag}
                GIT_SHALLOW    TRUE
                GIT_PROGRESS   TRUE
                FIND_PACKAGE_ARGS
        )
        FetchContent_MakeAvailable(${libName})
endfunction()

set(SDL_X11_XSCRNSAVER OFF)
include_dependency(SDL3 https://github.com/libsdl-org/SDL.git release-3.4.x)

# SDL_image (used for loading various image formats)
# set(SDLIMAGE_VENDORED ON)
# set(SDLIMAGE_AVIF OFF)	# disable formats we don't use to make the build faster and smaller.
# set(SDLIMAGE_BMP OFF)
# set(SDLIMAGE_JPEG OFF)
# set(SDLIMAGE_WEBP OFF)
# set(SDLIMAGE_TIFF OFF)
# set(SDLIMAGE_PNG_LIBPNG OFF)    # SDL3 includes built-in support for loading PNGs. If you need more than what that supports, enable this option.
# include_dependency(SDL3_image https://github.com/libsdl-org/SDL_image.git release-3.4.x)

include_dependency(SDL3_ttf https://github.com/libsdl-org/SDL_ttf.git release-3.2.x)

set(CSourceFiles "src/main.c" "src/cards.c")

set(TARGET_NAME ${PROJECT_NAME})
if (ANDROID)
        add_library(main SHARED ${CSourceFiles} deck.c)
        set(TARGET_NAME main)
else()
        add_executable(${TARGET_NAME} ${CSourceFiles} deck.c)
endif()

target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different ${CMAKE_SOURCE_DIR}/resources ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
        VERBATIM
)

target_include_directories(${TARGET_NAME} PUBLIC ${SDL3_INCLUDE_DIRS})

find_library(C_MATH_LIBRARY m)
if(C_MATH_LIBRARY)
    target_link_libraries(${TARGET_NAME} PUBLIC ${C_MATH_LIBRARY})
endif()

target_link_libraries(${TARGET_NAME} PRIVATE
        SDL3::SDL3
        # SDL3_image::SDL3_image
        SDL3_ttf::SDL3_ttf
)
