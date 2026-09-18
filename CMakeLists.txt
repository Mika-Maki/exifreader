cmake_minimum_required(VERSION 3.16)

project(exifreader
  VERSION 1.0.1
  DESCRIPTION "Image EXIF information-tree reader"
  HOMEPAGE_URL "https://github.com/Mika-Maki/exifreader"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
    Debug Release RelWithDebInfo MinSizeRel)
endif()

option(EXIFREADER_WERROR "Treat compiler warnings as errors" OFF)
option(EXIFREADER_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

add_executable(exifreader
  src/main.cpp
  src/container.cpp
  src/tiff_parser.cpp
  src/tiff_tags.cpp
  src/renderer.cpp
)

if(MSVC)
  target_compile_options(exifreader PRIVATE /W4)
else()
  target_compile_options(exifreader PRIVATE
    -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
  if(EXIFREADER_WERROR)
    target_compile_options(exifreader PRIVATE -Werror)
  endif()
  if(EXIFREADER_SANITIZE)
    target_compile_options(exifreader PRIVATE
      -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(exifreader PRIVATE -fsanitize=address,undefined)
  endif()
endif()

install(TARGETS exifreader RUNTIME DESTINATION bin)
install(FILES README.md CHANGELOG.md LICENSE
        DESTINATION share/doc/exifreader)

include(CTest)
if(BUILD_TESTING)
  add_test(NAME suite
           COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.sh"
                   "$<TARGET_FILE:exifreader>")
endif()

set(CPACK_PACKAGE_NAME "exifreader")
set(CPACK_PACKAGE_VENDOR "exifreader contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Dependency-free C++17 EXIF/TIFF information-tree reader")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Mika-Maki/exifreader")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
include(CPack)
