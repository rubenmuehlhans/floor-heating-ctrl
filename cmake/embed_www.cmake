# Legt eine Weboberflaeche komprimiert ins Programmabbild.
#
#   embed_www(TARGET  <komponentenbibliothek>
#             OUTPUT  <dateiname>
#             SOURCES <datei> [<datei> ...])
#
# Die Quelldateien werden in der angegebenen Reihenfolge aneinandergehaengt und
# danach mit gzip gepackt. Ausgeliefert wird zur Laufzeit eine einzige Datei:
# die Oberflaeche soll ohne Internetzugang und ohne zusaetzliche Partition
# laden.
#
# Der Symbolname folgt dem Dateinamen aus OUTPUT: aus "index.html.gz" werden
# _binary_index_html_gz_start und _binary_index_html_gz_end. Beide Anwendungen
# duerfen denselben Namen verwenden, ihre Bauverzeichnisse sind getrennt.

function(embed_www)
    cmake_parse_arguments(EW "" "TARGET;OUTPUT" "SOURCES" ${ARGN})

    if(NOT EW_TARGET OR NOT EW_OUTPUT OR NOT EW_SOURCES)
        message(FATAL_ERROR "embed_www: TARGET, OUTPUT und SOURCES sind Pflicht")
    endif()
    if(EW_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "embed_www: unbekannte Angabe ${EW_UNPARSED_ARGUMENTS}")
    endif()

    set(sources "")
    foreach(src IN LISTS EW_SOURCES)
        get_filename_component(abs "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if(NOT EXISTS "${abs}")
            message(FATAL_ERROR "embed_www: Quelldatei fehlt: ${abs}")
        endif()
        list(APPEND sources "${abs}")
    endforeach()

    set(packer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/embed_www.py")
    idf_build_get_property(python PYTHON)
    set(gz "${CMAKE_CURRENT_BINARY_DIR}/${EW_OUTPUT}")

    add_custom_command(
        OUTPUT "${gz}"
        COMMAND "${python}" "${packer}" "${gz}" ${sources}
        DEPENDS ${sources} "${packer}"
        COMMENT "Oberflaeche zusammenfuegen und komprimieren: ${EW_OUTPUT}"
        VERBATIM)

    string(MAKE_C_IDENTIFIER "www_gzip_${EW_OUTPUT}" stem)
    add_custom_target(${stem} DEPENDS "${gz}")
    add_dependencies(${EW_TARGET} ${stem})

    target_add_binary_data(${EW_TARGET} "${gz}" BINARY)
endfunction()
