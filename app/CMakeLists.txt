find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Network Svg)

qt_standard_project_setup(REQUIRES 6.5)

# The Piece Set in use is one thing, so its QML type is a singleton.
set_source_files_properties(qml/PieceSet.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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
    qml/PieceSet.qml
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
  # Piece Set artwork is vector, so the workspace needs the SVG image handler.
  Qt6::Svg
  omachess::core
)

install(TARGETS omachess RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${CMAKE_SOURCE_DIR}/packaging/com.omachess.Omachess.desktop"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
