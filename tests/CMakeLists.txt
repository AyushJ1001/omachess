find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# Journey tests drive the installed-shaped binary itself, so they only need the
# executable and the harness on the path.
add_test(NAME journey_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          --start-directory "${CMAKE_CURRENT_SOURCE_DIR}/journey"
          --verbose)

set_tests_properties(journey_tests PROPERTIES
  ENVIRONMENT "OMACHESS_BINARY=$<TARGET_FILE:omachess>"
  TIMEOUT 300)

# Packaging journeys run against an installed tree, not a build directory, so
# ctest stages one first with the same install rules makepkg uses.
set(_omachess_stage "${CMAKE_BINARY_DIR}/stage")

add_test(NAME staged_install
  COMMAND "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}"
          --prefix "${_omachess_stage}/usr")
set_tests_properties(staged_install PROPERTIES FIXTURES_SETUP staged_install)

add_test(NAME packaging_tests
  COMMAND "${Python3_EXECUTABLE}" -m unittest discover
          --start-directory "${CMAKE_CURRENT_SOURCE_DIR}/packaging"
          --verbose)

set_tests_properties(packaging_tests PROPERTIES
  FIXTURES_REQUIRED staged_install
  ENVIRONMENT "OMACHESS_INSTALL_PREFIX=${_omachess_stage}/usr"
  TIMEOUT 300)
