cmake_minimum_required(VERSION 3.20)
project(SafeEditor VERSION 1.0.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MEMPAD_CORE_SOURCES
    src/security/process_guard.cpp
    src/security/secure_allocation.cpp
    src/security/wer_guard.cpp
    src/encoding/utf8.cpp
    src/encoding/encoding.cpp
    src/document/secure_gap_buffer.cpp
    src/document/secure_document.cpp
    src/editor/clipboard_text.cpp
    src/editor/line_index.cpp
    src/editor/selection.cpp
    src/editor/text_navigation.cpp
    src/editor/ini_env_highlight.cpp
)

add_library(mempad_core STATIC ${MEMPAD_CORE_SOURCES})
target_include_directories(mempad_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(mempad_core PRIVATE
    -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type
)

if(WIN32)
    target_compile_options(mempad_core PRIVATE
        $<$<CONFIG:Release>:-O2;-fno-exceptions;-fno-rtti;-fstack-protector-strong>
    )
    add_executable(SafeEditor WIN32
        src/main.cpp
        src/app/application.cpp
        src/app/main_window.cpp
        src/app/shell_integration.cpp
        src/editor/editor_control.cpp
        src/io/file_reader.cpp
        src/io/file_writer.cpp
        src/io/file_dialog.cpp
        resources/SafeEditor.rc
    )
    set_target_properties(SafeEditor PROPERTIES OUTPUT_NAME "mempad")
    target_include_directories(SafeEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(SafeEditor PRIVATE
        UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN
        SAFEEDITOR_VERSION=L\"${PROJECT_VERSION}\"
    )
    target_compile_options(SafeEditor PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type
        $<$<CONFIG:Release>:-O2;-fno-exceptions;-fno-rtti;-fstack-protector-strong>
    )
    target_link_options(SafeEditor PRIVATE
        -municode
        $<$<CONFIG:Release>:-static;-static-libgcc;-static-libstdc++;-mwindows;
        -Wl,--dynamicbase;-Wl,--nxcompat;-Wl,--high-entropy-va;-Wl,--tsaware;
        -Wl,--no-insert-timestamp>
    )
    target_link_libraries(SafeEditor PRIVATE mempad_core kernel32 user32 gdi32 comdlg32
        shell32 ole32 uuid advapi32 wtsapi32 imm32 dwmapi uxtheme)
    option(MEMPAD_BUILD_EDITOR_TESTS "Build native Windows editor interaction tests" OFF)
    if(MEMPAD_BUILD_EDITOR_TESTS)
        enable_testing()
        add_executable(mempad_editor_tests
            tests/editor_control_tests.cpp
            src/editor/editor_control.cpp
        )
        target_include_directories(mempad_editor_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
        target_compile_definitions(mempad_editor_tests PRIVATE
            UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mempad_editor_tests PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type)
        target_link_options(mempad_editor_tests PRIVATE -static -static-libgcc -static-libstdc++)
        target_link_libraries(mempad_editor_tests PRIVATE mempad_core kernel32 user32 gdi32)
        add_test(NAME mempad_editor_tests COMMAND mempad_editor_tests)
        set_tests_properties(mempad_editor_tests PROPERTIES TIMEOUT 30)
    endif()
else()
    enable_testing()
    add_executable(mempad_tests
        tests/test_main.cpp
        tests/clipboard_text_tests.cpp
        tests/encoding_tests.cpp
        tests/gap_buffer_tests.cpp
        tests/document_tests.cpp
        tests/text_navigation_tests.cpp
        tests/roundtrip_tests.cpp
        tests/ini_env_highlight_tests.cpp
    )
    target_include_directories(mempad_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    target_link_libraries(mempad_tests PRIVATE mempad_core)
    target_compile_options(mempad_tests PRIVATE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror=return-type
    )
    add_test(NAME mempad_tests COMMAND mempad_tests)
endif()
