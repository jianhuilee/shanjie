# Installs the build into a scratch DESTDIR and checks that the data ships with its licenses.
# Messages name only files of this repository's install manifest.
set(dest "${BUILD_DIR}/test-install")
file(REMOVE_RECURSE "${dest}")
execute_process(COMMAND ${CMAKE_COMMAND} -E env DESTDIR=${dest} ${CMAKE_COMMAND} --install ${BUILD_DIR}
                RESULT_VARIABLE rc OUTPUT_QUIET)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "install into DESTDIR failed")
endif()
set(root "${dest}${PKGDATA}/shanjie")
set(expected lexicon/mcbpmf-data.txt lexicon/english.tsv packs/acg-add.tsv
             LICENSES/LICENSE LICENSES/data.md LICENSES/McBopomofo-MIT.txt
             LICENSES/name-suggestion-index-BSD-3-Clause.txt LICENSES/CC-BY-SA-4.0-attribution.txt)
if(EXISTS "${REPO_ROOT}/data/lm/bigram.sjlm")
    list(APPEND expected lm/colloquial.sjlm LICENSES/colloquial-attribution.tsv)
endif()
set(missing "")
foreach(f IN LISTS expected)
    if(NOT EXISTS "${root}/${f}")
        list(APPEND missing "${f}")
    endif()
endforeach()
if(missing)
    message(FATAL_ERROR "not installed: ${missing}")
endif()
# The attribution text is the app's, start to end.
file(READ "${root}/LICENSES/CC-BY-SA-4.0-attribution.txt" attr)
if(NOT attr MATCHES "^overlay-add.tsv, names-add.tsv" OR NOT attr MATCHES "name-suggestion-index notice")
    message(FATAL_ERROR "CC-BY-SA-4.0-attribution.txt is not the app's attribution text")
endif()
file(REMOVE_RECURSE "${dest}")
