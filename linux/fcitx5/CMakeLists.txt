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
# The target directory is passed explicitly: cargo otherwise honours CARGO_TARGET_DIR and
# build.target-dir, and the archive linked below would then be missing or a stale one from another build.
set(CORE_TARGET_DIR "${REPO_ROOT}/target")
set(CORE_LIB "${CORE_TARGET_DIR}/release/libcore.a")
add_custom_target(shanjie_core_build
    COMMAND cargo build --release --locked -p core --target-dir "${CORE_TARGET_DIR}"
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
    src/task_thread.cpp
    src/session.cpp)
target_include_directories(shanjie_shell PUBLIC src)
target_link_libraries(shanjie_shell PUBLIC shanjie_core)

# --- The fcitx5 addon
# fcitx5 loads addon libraries only from its own library directory. Without this, the default prefix
# (/usr/local) would install a library fcitx5 never finds. Defined before find_package so that it is the
# default; pass -DFCITX_INSTALL_USE_FCITX_SYS_PATHS=OFF with FCITX_ADDON_DIRS set for a private install.
option(FCITX_INSTALL_USE_FCITX_SYS_PATHS "Install into the directories fcitx5 itself uses" ON)
# 5.1.13: the first release with fcitx-utils/standardpaths.h (StandardPaths); every other API used is older.
find_package(Fcitx5Core 5.1.13 REQUIRED)
message(STATUS "Shanjie addon library goes to ${FCITX_INSTALL_ADDONDIR}")
add_library(shanjie MODULE
    src/candidates.cpp
    src/engine.cpp)
target_link_libraries(shanjie PRIVATE shanjie_shell Fcitx5::Core)
# Keep the Rust runtime's symbols out of the fcitx5 process's global namespace.
target_link_options(shanjie PRIVATE -Wl,--exclude-libs,ALL)

install(TARGETS shanjie DESTINATION "${FCITX_INSTALL_ADDONDIR}")
install(FILES src/shanjie-addon.conf RENAME shanjie.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/addon")
install(FILES src/shanjie-im.conf RENAME shanjie.conf DESTINATION "${FCITX_INSTALL_PKGDATADIR}/inputmethod")

# The files the core reads from its data directory (docs/spec/c-abi.md §8).
set(LEXICON_FILES mcbpmf-data.txt overlay-add.tsv sandhi-add.tsv names-add.tsv variant-order.tsv
    demote.tsv rerank-protect.tsv std-exempt.tsv digits.tsv mixed-words.tsv english.tsv)
list(TRANSFORM LEXICON_FILES PREPEND "${REPO_ROOT}/data/lexicon/")
install(FILES ${LEXICON_FILES} DESTINATION "${FCITX_INSTALL_PKGDATADIR}/shanjie/lexicon")

# The packs, with the per-word sources and the manifests the attribution text points to (as the app ships them).
set(PACK_FILES acg-add.tsv acg-sources.tsv acg.json quotes-add.tsv quotes.json it-add.tsv it-sources.tsv it.json
    meme-add.tsv meme-sources.tsv meme.json beauty-add.tsv beauty-sources.tsv beauty.json)
list(TRANSFORM PACK_FILES PREPEND "${REPO_ROOT}/data/packs/")
install(FILES ${PACK_FILES} DESTINATION "${FCITX_INSTALL_PKGDATADIR}/shanjie/packs")

# Models are not in git. When they were downloaded into data/lm, install them too.
set(MODEL_FILES bigram.sjlm classes.sjc kn.sjkn tinylm.sjtl trigram.sjt3 trigram-chat.sjt3 colloquial.sjlm)
list(TRANSFORM MODEL_FILES PREPEND "${REPO_ROOT}/data/lm/")
if(EXISTS "${REPO_ROOT}/data/lm/bigram.sjlm")
    install(FILES ${MODEL_FILES} DESTINATION "${FCITX_INSTALL_PKGDATADIR}/shanjie/lm")
    # The per-source credits of the colloquial table's public sentences go with it (docs/spec/semantic-rerank.md §4.7).
    install(FILES "${REPO_ROOT}/data/lm/colloquial-attribution.tsv" DESTINATION "${FCITX_INSTALL_PKGDATADIR}/shanjie/LICENSES")
else()
    message(WARNING "data/lm/bigram.sjlm not found: models will not be installed (see CONTRIBUTING.md)")
endif()

# Licenses and attribution of the data shipped above, as the macOS app carries them (scripts/build-app.sh;
# the list of every source is LICENSES/data.md). The CC BY-SA attribution text is taken from the app
# script's heredoc, so the two packagings cannot drift apart. Sparkle is macOS-only and not installed.
set(BUILD_APP "${REPO_ROOT}/scripts/build-app.sh")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BUILD_APP}")
file(READ "${BUILD_APP}" BUILD_APP_TEXT)
string(FIND "${BUILD_APP_TEXT}" "CC-BY-SA-4.0-attribution.txt\" <<'EOF'\n" ATTR_AT)
if(ATTR_AT EQUAL -1)
    message(FATAL_ERROR "scripts/build-app.sh no longer writes CC-BY-SA-4.0-attribution.txt from a heredoc")
endif()
string(SUBSTRING "${BUILD_APP_TEXT}" ${ATTR_AT} -1 ATTR_TEXT)
string(FIND "${ATTR_TEXT}" "\n" ATTR_START)
math(EXPR ATTR_START "${ATTR_START} + 1")
string(SUBSTRING "${ATTR_TEXT}" ${ATTR_START} -1 ATTR_TEXT)
string(FIND "${ATTR_TEXT}" "\nEOF\n" ATTR_END)
if(ATTR_END EQUAL -1)
    message(FATAL_ERROR "scripts/build-app.sh: the attribution heredoc has no end")
endif()
math(EXPR ATTR_END "${ATTR_END} + 1")
string(SUBSTRING "${ATTR_TEXT}" 0 ${ATTR_END} ATTR_TEXT)
# The app script appends the name-suggestion-index notice after the heredoc; the text promises it "in full".
if(NOT BUILD_APP_TEXT MATCHES "cat LICENSES/name-suggestion-index-BSD-3-Clause.txt >> \"\\$RES/LICENSES/CC-BY-SA-4.0-attribution.txt\"")
    message(FATAL_ERROR "scripts/build-app.sh no longer appends the name-suggestion-index notice to the attribution text")
endif()
file(READ "${REPO_ROOT}/LICENSES/name-suggestion-index-BSD-3-Clause.txt" NSI_TEXT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${REPO_ROOT}/LICENSES/name-suggestion-index-BSD-3-Clause.txt")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/CC-BY-SA-4.0-attribution.txt" "${ATTR_TEXT}${NSI_TEXT}")
install(FILES "${REPO_ROOT}/LICENSE" "${REPO_ROOT}/LICENSES/data.md" "${REPO_ROOT}/LICENSES/McBopomofo-MIT.txt"
              "${REPO_ROOT}/LICENSES/name-suggestion-index-BSD-3-Clause.txt"
              "${CMAKE_CURRENT_BINARY_DIR}/CC-BY-SA-4.0-attribution.txt"
        DESTINATION "${FCITX_INSTALL_PKGDATADIR}/shanjie/LICENSES")

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
shanjie_test(test_fcitx_key shanjie_shell Fcitx5::Utils)
shanjie_test(test_pack_switch shanjie_shell)
shanjie_test(test_fcitx_policy shanjie_shell Fcitx5::Core)

# The install manifest: installs into a scratch DESTDIR and checks the data and its licenses are there.
add_test(NAME test_install_manifest
         COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DPKGDATA=${FCITX_INSTALL_PKGDATADIR}
                 -DREPO_ROOT=${REPO_ROOT} -P ${CMAKE_CURRENT_SOURCE_DIR}/test/check_install.cmake)
