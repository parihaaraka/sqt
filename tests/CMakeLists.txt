# Unit tests for the parts that carry no db/gui dependencies.
#
# Qt6::Test comes from the one find_package(Qt6) in cmake/FindOrInstallQt.cmake,
# which asks for it as an optional component. No find_package here on purpose:
# a second one re-includes Qt's config files, and on MinGW that dies outright on
# Qt6EntryPointMinGW32Target.cmake, which creates its imported target with no
# `if(NOT TARGET ...)` guard. See the comment in that module.
#
# Optional there, so the tests still step aside on a Qt without the Test module
# instead of breaking the application build.
if(NOT TARGET Qt6::Test)
    message(WARNING "Qt6 Test module not found - unit tests are not built")
    return()
endif()

set(SQT_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

# Each test compiles the units it covers directly: they are self-contained by
# design, and linking the application target would drag in libpq, odbc and gui.
function(sqt_add_test name)
    add_executable(tst_${name} tst_${name}.cpp ${ARGN})
    target_include_directories(tst_${name} PRIVATE "${SQT_SRC_DIR}")
    target_link_libraries(tst_${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND tst_${name})
endfunction()

sqt_add_test(scriptversionfilter "${SQT_SRC_DIR}/scriptversionfilter.cpp")
sqt_add_test(decimalsum "${SQT_SRC_DIR}/decimalsum.cpp")
sqt_add_test(resourcelocator "${SQT_SRC_DIR}/resourcelocator.cpp")
sqt_add_test(queryoptions "${SQT_SRC_DIR}/queryoptions.cpp")
sqt_add_test(sqlparser "${SQT_SRC_DIR}/sqlparser.cpp")
sqt_add_test(textcodec "${SQT_SRC_DIR}/textcodec.cpp")
sqt_add_test(pgtypmod "${SQT_SRC_DIR}/pgtypmod.cpp")
# word-wise caret movement: free functions over a QTextDocument (QtGui, but no
# widget and no event loop), so the Ctrl+Left/Right rules are testable on their own
sqt_add_test(wordnav "${SQT_SRC_DIR}/wordnav.cpp")
target_link_libraries(tst_wordnav PRIVATE Qt6::Gui)
# the file search engine carries no gui/db dependency on purpose, so its mask
# and matching rules are testable; textcodec comes along as it decodes the files
sqt_add_test(filesearch "${SQT_SRC_DIR}/filesearch.cpp" "${SQT_SRC_DIR}/textcodec.cpp")

# The palette arithmetic behind the unfocused selection is pure computation, so
# it is worth pinning down. Unlike the rest it does need QtWidgets (it reads a
# QWidget's palette) and hence a QApplication - so the test runs offscreen, which
# keeps it usable on a build machine with no display.
if(TARGET Qt6::Widgets)
    sqt_add_test(styling "${SQT_SRC_DIR}/styling.cpp")
    target_link_libraries(tst_styling PRIVATE Qt6::Gui Qt6::Widgets)
    set_tests_properties(styling PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()




