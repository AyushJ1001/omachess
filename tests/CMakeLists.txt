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
