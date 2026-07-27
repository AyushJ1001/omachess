find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Network Svg Test Widgets DBus)

qt_standard_project_setup(REQUIRES 6.5)

# The Piece Set in use is one thing, so its QML type is a singleton.
set_source_files_properties(qml/PieceSet.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/ActionRegistry.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_executable(omachess
  src/main.cpp
  src/BoardModel.cpp
  src/BoardModel.h
  src/TestChannel.cpp
  src/TestChannel.h
  src/EngineManager.cpp
  src/EngineManager.h
)

qt_add_qml_module(omachess
  URI Omachess
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/Board.qml
    qml/Square.qml
    qml/PieceSet.qml
    qml/ActionRegistry.qml
  RESOURCES
    qml/pieces/cburnett/white_king.svg
    qml/pieces/cburnett/white_queen.svg
    qml/pieces/cburnett/white_rook.svg
    qml/pieces/cburnett/white_bishop.svg
    qml/pieces/cburnett/white_knight.svg
    qml/pieces/cburnett/white_pawn.svg
    qml/pieces/cburnett/black_king.svg
    qml/pieces/cburnett/black_queen.svg
    qml/pieces/cburnett/black_rook.svg
    qml/pieces/cburnett/black_bishop.svg
    qml/pieces/cburnett/black_knight.svg
    qml/pieces/cburnett/black_pawn.svg
    qml/engine-art/stockfish.svg
    qml/engine-art/leela.svg
    qml/engine-art/reckless.svg
    qml/engine-art/README.md
  SOURCES
    src/WorkspaceSession.h
    src/WorkspaceSession.cpp
    src/OmarchyAdapter.h
    src/OmarchyAdapter.cpp
    src/ThemeController.h
    src/ThemeController.cpp
)

target_compile_definitions(omachess PRIVATE OMACHESS_VERSION="${PROJECT_VERSION}")
target_include_directories(omachess PRIVATE src)

target_link_libraries(omachess PRIVATE
  Qt6::Quick
  Qt6::QuickControls2
  Qt6::Network
  # Piece Set artwork is vector, so the workspace needs the SVG image handler.
  Qt6::Svg
  Qt6::Test
  Qt6::Widgets
  Qt6::DBus
  omachess::core
)

# This executable is deliberately separate from the workspace. D-Bus starts
# it on demand and it may survive the workspace window closing.
qt_add_executable(omachess-background-worker
  src/BackgroundWorker.cpp
)
target_link_libraries(omachess-background-worker PRIVATE Qt6::Core Qt6::DBus omachess::core)
install(TARGETS omachess-background-worker RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
configure_file(
  "${CMAKE_SOURCE_DIR}/packaging/com.omachess.Omachess.BackgroundWorker.service.in"
  "${CMAKE_CURRENT_BINARY_DIR}/com.omachess.Omachess.BackgroundWorker.service"
  @ONLY
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/com.omachess.Omachess.BackgroundWorker.service"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/dbus-1/services")

install(TARGETS omachess RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/packaging/com.omachess.Omachess.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")

# The launcher entry names this icon, so the two travel together.
install(FILES "${CMAKE_SOURCE_DIR}/packaging/icons/com.omachess.Omachess.svg"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")

# Recipient-facing legal, source, release, and recovery guidance ships with
# the package rather than depending on a repository checkout.
install(FILES
  "${CMAKE_SOURCE_DIR}/docs/install.md"
  "${CMAKE_SOURCE_DIR}/docs/backup.md"
  "${CMAKE_SOURCE_DIR}/docs/THIRD_PARTY_NOTICES.md"
  "${CMAKE_SOURCE_DIR}/docs/CORRESPONDING_SOURCE.md"
  "${CMAKE_SOURCE_DIR}/docs/RELEASE_NOTES_0.1.md"
  DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/LICENSE"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/omachess")
