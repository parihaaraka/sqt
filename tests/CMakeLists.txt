# Unit tests for the parts that carry no db/gui dependencies.
# Qt6::Test lives in qtbase, but the module is not requested by the main build,
# so the tests quietly step aside instead of breaking it when it is missing.
find_package(Qt6 QUIET COMPONENTS Test)
if(NOT Qt6Test_FOUND)
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

