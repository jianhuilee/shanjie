cmake_minimum_required(VERSION 3.21)
project(fcitx5-shanjie VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-Wall -Wextra)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)

# --- The Rust core: built by cargo, linked as a static library. Header and library come from the same
# checkout (docs/spec/c-abi.md §2.1).
set(CORE_LIB "${REPO_ROOT}/target/release/libcore.a")
add_custom_target(shanjie_core_build
    COMMAND cargo build --release --locked -p core
    WORKING_DIRECTORY "${REPO_ROOT}"
    BYPRODUCTS "${CORE_LIB}"
    COMMENT "cargo build -p core")
add_library(shanjie_core STATIC IMPORTED GLOBAL)
set_target_properties(shanjie_core PROPERTIES IMPORTED_LOCATION "${CORE_LIB}")
target_include_directories(shanjie_core INTERFACE "${REPO_ROOT}/core/include")
# The list `cargo rustc -p core --crate-type staticlib -- --print native-static-libs` reports on Linux.
target_link_libraries(shanjie_core INTERFACE gcc_s util rt Threads::Threads m dl)
add_dependencies(shanjie_core shanjie_core_build)

# --- Shell logic without any fcitx5 dependency
add_library(shanjie_shell STATIC
    src/utf.cpp
    src/keymap.cpp
    src/core_engine.cpp
    src/rerank_runner.cpp
    src/session.cpp)
target_include_directories(shanjie_shell PUBLIC src)
target_link_libraries(shanjie_shell PUBLIC shanjie_core)

# --- Tests
enable_testing()
set(TEST_DEFS
    SHANJIE_TEST_LEXICON="${REPO_ROOT}/data/lexicon"
    SHANJIE_TEST_PACKS="${REPO_ROOT}/data/packs"
    SHANJIE_TEST_LM="${REPO_ROOT}/data/lm")

function(shanjie_test name)
    add_executable(${name} test/${name}.cpp)
    target_compile_definitions(${name} PRIVATE ${TEST_DEFS})
    target_include_directories(${name} PRIVATE test)
    target_link_libraries(${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

shanjie_test(test_core_link shanjie_core)
shanjie_test(test_utf shanjie_shell)
shanjie_test(test_keymap shanjie_shell)
shanjie_test(test_core_engine shanjie_shell)
shanjie_test(test_rerank_runner shanjie_shell)
shanjie_test(test_session shanjie_shell)
