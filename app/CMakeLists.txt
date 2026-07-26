find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Network)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(omachess
  src/main.cpp
  src/BoardModel.cpp
  src/BoardModel.h
  src/TestChannel.cpp
  src/TestChannel.h
)

qt_add_qml_module(omachess
  URI Omachess
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Board.qml
    qml/Square.qml
  SOURCES
    src/WorkspaceSession.h
    src/WorkspaceSession.cpp
)

target_compile_definitions(omachess PRIVATE OMACHESS_VERSION="${PROJECT_VERSION}")
target_include_directories(omachess PRIVATE src)

target_link_libraries(omachess PRIVATE
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Network
  omachess::core
)

install(TARGETS omachess RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/packaging/com.omachess.Omachess.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
