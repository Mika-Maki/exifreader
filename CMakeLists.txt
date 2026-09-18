cmake_minimum_required(VERSION 3.16)

project(exifreader
  VERSION 1.0.3
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
option(EXIFREADER_FUZZ "Build the libFuzzer harness (requires clang)" OFF)
# ASan needs a user address space wider than 39 bits; on kernels that cannot map
# its shadow, build with -DEXIFREADER_FUZZ_SANITIZERS=fuzzer,undefined instead.
set(EXIFREADER_FUZZ_SANITIZERS "fuzzer,address" CACHE STRING
    "Sanitizers for exifreader_fuzz")

# The parsers and renderers live in a library without the CLI entry point, so
# both the command line tool and the fuzz harness can link them.
add_library(exifreader_lib STATIC
  src/container.cpp
  src/tiff_parser.cpp
  src/tiff_tags.cpp
  src/renderer.cpp
)
target_include_directories(exifreader_lib PUBLIC src)

add_executable(exifreader src/main.cpp)
target_link_libraries(exifreader PRIVATE exifreader_lib)

foreach(target exifreader exifreader_lib)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE
      -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
    if(EXIFREADER_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(EXIFREADER_SANITIZE)
      target_compile_options(${target} PRIVATE
        -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
endforeach()

if(EXIFREADER_FUZZ)
  add_executable(exifreader_fuzz fuzz/fuzz_exif.cc)
  target_link_libraries(exifreader_fuzz PRIVATE exifreader_lib)
  target_compile_options(exifreader_fuzz PRIVATE
    -fsanitize=${EXIFREADER_FUZZ_SANITIZERS} -fno-omit-frame-pointer)
  target_link_options(exifreader_fuzz PRIVATE
    -fsanitize=${EXIFREADER_FUZZ_SANITIZERS})
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
