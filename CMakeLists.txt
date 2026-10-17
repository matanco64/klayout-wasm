# klayout-wasm: KLayout's layout view and editors as a WebAssembly module
# with a JavaScript API (Embind). Configure with Emscripten's toolchain:
#
#   emcmake cmake -B build -G Ninja && cmake --build build
#
# Outputs build/klayout.js, build/klayout.wasm and build/klayout.d.ts.

cmake_minimum_required (VERSION 3.24)
project (klayout_wasm C CXX)

if (NOT EMSCRIPTEN)
  message (FATAL_ERROR "configure with Emscripten: emcmake cmake -B build -G Ninja")
endif ()

set (KLAYOUT_SRC ${CMAKE_SOURCE_DIR}/vendor/klayout/src)
set (EXPAT_SRC ${CMAKE_SOURCE_DIR}/vendor/libexpat/expat/lib)
if (NOT EXISTS ${KLAYOUT_SRC}/klayout.pri OR NOT EXISTS ${EXPAT_SRC}/xmlparse.c)
  message (FATAL_ERROR "vendored sources missing; run: git submodule update --init --depth 1")
endif ()

# KLayout's source lists, read from its .pro files, plus patched copies of
# the two files Emscripten needs edited. Re-run when either changes.
find_package (Python3 REQUIRED COMPONENTS Interpreter)
execute_process (
  COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_SOURCE_DIR}/tools/klayout_sources.py ${KLAYOUT_SRC} ${CMAKE_BINARY_DIR}/klayout
  RESULT_VARIABLE sources_result)
if (NOT sources_result EQUAL 0)
  message (FATAL_ERROR "tools/klayout_sources.py failed")
endif ()
include (${CMAKE_BINARY_DIR}/klayout/klayout_sources.cmake)
set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/klayout_sources.py ${KL_PRO_FILES})

file (STRINGS ${CMAKE_SOURCE_DIR}/vendor/klayout/version.sh version_line REGEX "^KLAYOUT_VERSION=")
string (REGEX REPLACE "^KLAYOUT_VERSION=\"([^\"]*)\".*" "\\1" KLAYOUT_VERSION "${version_line}")
set_property (DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/vendor/klayout/version.sh)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_EXPORT_COMPILE_COMMANDS ON)
# KLayout is far too large to iterate on at -O0, and a debug build of it is
# not what anyone debugs in the browser. No NDEBUG: KLayout keeps its own
# assertions (tl_assert) either way.
set (CMAKE_C_FLAGS_RELEASE "-O2")
set (CMAKE_CXX_FLAGS_RELEASE "-O2")
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release)
endif ()

# Native WebAssembly exceptions, in the legacy encoding every current
# browser runs; compile and link must agree. zlib is Emscripten's port.
set (WASM_FLAGS -fwasm-exceptions -sWASM_LEGACY_EXCEPTIONS=1 -sUSE_ZLIB=1)
add_compile_options (${WASM_FLAGS})

set (KLAYOUT_INCLUDES
  ${KLAYOUT_SRC}/tl/tl
  ${KLAYOUT_SRC}/gsi/gsi
  ${KLAYOUT_SRC}/db/db
  ${KLAYOUT_SRC}/version
  ${KLAYOUT_SRC}/plugins/common
  ${KLAYOUT_SRC}/plugins/streamers/gds2/db_plugin
  ${KLAYOUT_SRC}/plugins/streamers/oasis/db_plugin
  ${KLAYOUT_SRC}/rdb/rdb
  ${KLAYOUT_SRC}/laybasic/laybasic
  ${KLAYOUT_SRC}/layview/layview
  ${KLAYOUT_SRC}/edt/edt
  ${KLAYOUT_SRC}/ant/ant
  ${EXPAT_SRC})

# Expat: KLayout's XML parser when built without Qt. The handful of facts
# expat's own CMake would have detected.
file (WRITE ${CMAKE_BINARY_DIR}/expat/expat_config.h
  "#define BYTEORDER 1234\n#define HAVE_GETENTROPY 1\n#define HAVE_MEMMOVE 1\n"
  "#define HAVE_STDINT_H 1\n#define HAVE_STDLIB_H 1\n#define HAVE_STRING_H 1\n"
  "#define HAVE_UNISTD_H 1\n#define XML_DTD 1\n#define XML_GE 1\n#define XML_NS 1\n"
  "#define XML_CONTEXT_BYTES 1024\n")
add_library (expat OBJECT
  ${EXPAT_SRC}/xmlparse.c ${EXPAT_SRC}/xmlrole.c ${EXPAT_SRC}/xmltok.c
  ${EXPAT_SRC}/xcs.c ${EXPAT_SRC}/random_getentropy.c)
target_include_directories (expat PRIVATE ${CMAKE_BINARY_DIR}/expat ${EXPAT_SRC})
target_compile_definitions (expat PRIVATE HAVE_EXPAT_CONFIG_H XML_STATIC)
target_compile_options (expat PRIVATE -w)

# One object library per KLayout module, each with its own MAKE_* define.
# Object files are linked directly, so the static registrations KLayout
# relies on (stream formats, gsi classes) are never dropped.
set (KLAYOUT_OBJECTS $<TARGET_OBJECTS:expat>)
foreach (module ${KL_MODULES})
  add_library (kl_${module} OBJECT ${KL_${module}_SOURCES})
  target_include_directories (kl_${module} PRIVATE ${KLAYOUT_INCLUDES})
  target_compile_definitions (kl_${module} PRIVATE ${KL_${module}_DEFINE} HAVE_EXPAT)
  target_compile_options (kl_${module} PRIVATE -w)
  list (APPEND KLAYOUT_OBJECTS $<TARGET_OBJECTS:kl_${module}>)
endforeach ()

add_executable (klayout src/bindings.cc src/convert.cc src/view.cc ${KLAYOUT_OBJECTS})
target_include_directories (klayout PRIVATE ${KLAYOUT_INCLUDES} src)
target_compile_definitions (klayout PRIVATE HAVE_EXPAT KLAYOUT_VERSION="${KLAYOUT_VERSION}")
target_compile_options (klayout PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_options (klayout PRIVATE
  ${WASM_FLAGS}
  -O2
  -lembind
  --emit-tsd klayout.d.ts
  -sALLOW_MEMORY_GROWTH=1
  -sSTACK_SIZE=4MB
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createKlayout
  -sENVIRONMENT=web,worker,node)
