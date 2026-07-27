# Builds the Rust core with cargo and exposes it as the omachess::core target.
#
# The core is linked as a static library, so the workspace binary carries the
# chess core with it and needs no runtime library search path.

find_program(CARGO_EXECUTABLE cargo REQUIRED
  DOC "The cargo build tool, used to build the Rust core")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(_omachess_cargo_profile dev)
  set(_omachess_cargo_dir debug)
else()
  set(_omachess_cargo_profile release)
  set(_omachess_cargo_dir release)
endif()

set(_omachess_cargo_target_dir "${CMAKE_BINARY_DIR}/cargo")
set(_omachess_core_library
  "${_omachess_cargo_target_dir}/${_omachess_cargo_dir}/libomachess_core.a")

# cargo does its own up-to-date checking, so this runs on every build and is a
# no-op when nothing in core/ changed.
add_custom_target(omachess_core_build ALL
  COMMAND "${CARGO_EXECUTABLE}" build
          --profile ${_omachess_cargo_profile}
          --package omachess-core
          --target-dir "${_omachess_cargo_target_dir}"
  BYPRODUCTS "${_omachess_core_library}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  COMMENT "Building the Omachess Rust core (${_omachess_cargo_profile})"
  VERBATIM)

add_library(omachess_core STATIC IMPORTED GLOBAL)
add_dependencies(omachess_core omachess_core_build)
set_target_properties(omachess_core PROPERTIES
  IMPORTED_LOCATION "${_omachess_core_library}"
  INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/core/include")
# The Rust standard library needs these from the host system.
find_package(SQLite3 REQUIRED)
target_link_libraries(omachess_core INTERFACE pthread dl m SQLite3::SQLite3)

add_library(omachess::core ALIAS omachess_core)

# `cargo test` covers the core and Live Store; ctest runs them alongside the
# journey tests.
add_test(NAME core_unit_tests
  COMMAND "${CARGO_EXECUTABLE}" test --workspace
          --target-dir "${_omachess_cargo_target_dir}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
