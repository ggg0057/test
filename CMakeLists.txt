cmake_minimum_required(VERSION 3.15)
project(chat_dictionary_server LANGUAGES C CXX)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/sqlite/sqlite3.c" OR
   NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/sqlite/sqlite3.h")
    execute_process(
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/restore_sqlite.ps1"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE SQLITE_RESTORE_RESULT
    )
    if(NOT SQLITE_RESTORE_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not restore the embedded SQLite source")
    endif()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(chat_server src/main.cpp src/persistence.cpp src/vendor/sqlite/sqlite3.c)
target_compile_definitions(chat_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_definitions(chat_server PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(chat_server PRIVATE ws2_32 shell32 bcrypt)

if(MINGW)
    target_link_options(chat_server PRIVATE -static -static-libgcc -static-libstdc++)
endif()
