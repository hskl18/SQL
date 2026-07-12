cmake_minimum_required(VERSION 3.20)

project(SQLInCPP VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SQL_CORE_SOURCES
    includes/binary_files/file_record.cpp
    includes/binary_files/utilities.cpp
    includes/parser/parser.cpp
    includes/shunting_yard/shunting_yard.cpp
    includes/sql/sql.cpp
    includes/table/table.cpp
    includes/tokenizer/ftokenize.cpp
    includes/tokenizer/state_machine_functions.cpp
    includes/tokenizer/stokenize.cpp
)

add_library(sql_core STATIC ${SQL_CORE_SOURCES})
target_include_directories(sql_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_options(sql_core PRIVATE /W4)
else()
    target_compile_options(sql_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(run_sql run_sql.cpp)
target_link_libraries(run_sql PRIVATE sql_core)

include(CTest)

if(BUILD_TESTING)
    add_executable(sql_tests tests/sql_tests.cpp)
    target_link_libraries(sql_tests PRIVATE sql_core)
    target_compile_definitions(sql_tests PRIVATE
        SQL_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/batch"
    )

    set(SQL_TEST_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/test-workspace")
    file(MAKE_DIRECTORY "${SQL_TEST_WORKING_DIRECTORY}")

    add_test(NAME sql_behavior COMMAND sql_tests)
    set_tests_properties(sql_behavior PROPERTIES
        WORKING_DIRECTORY "${SQL_TEST_WORKING_DIRECTORY}"
    )
endif()
