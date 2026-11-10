# Bedientafel: die App der Fußbodenheizung auf dem M5Stack Tab5 (ESP32-P4, 5″,
# 1280 × 720). Gleiche Bereiche wie die App auf iPhone, iPad und Mac: Übersicht,
# Räume, Assistent, Heizung, Geräte. Siehe README.md in diesem Verzeichnis.
#
# Aufbau nach dem Vorbild des Tab5 von camperSense (satellite-display/tab5):
# Funk über den ESP32-C6 (ESP-Hosted), Oberfläche in LVGL, Netz in eigenen Tasks.
#
# Bauen nur mit ESP-IDF 6.0.2, wie Verteiler und Heizungsgerät:
#   . ~/esp/esp-idf-6.0.2/export.sh
#   idf.py -C apps/tab5 build
cmake_minimum_required(VERSION 3.16)
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/version.txt)
file(READ ${CMAKE_CURRENT_LIST_DIR}/version.txt TAFEL_VER_RAW)
string(STRIP "${TAFEL_VER_RAW}" TAFEL_VER)
execute_process(COMMAND git rev-parse --short HEAD
                WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
                OUTPUT_VARIABLE TAFEL_GIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(TAFEL_GIT)
    set(PROJECT_VER "${TAFEL_VER}+tab5 (${TAFEL_GIT})")
else()
    set(PROJECT_VER "${TAFEL_VER}+tab5")
endif()

idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${REPO_ROOT}=." APPEND)

project(tafel)

# ESP-Hosted 3.0.9 überspringt einen Lesevorgang, wenn der Lesepuffer nicht wachsen
# kann; danach warten P4 und C6 dauerhaft aufeinander (camperSense, tab5/CLAUDE.md
# Falle 68; esp-hosted-mcu#167). Der Patch wartet stattdessen auf Speicher. Passt er
# nicht mehr, bricht der Bau ab, statt den Schutz still zu verlieren.
set(TAFEL_EH_DIR ${CMAKE_CURRENT_LIST_DIR}/managed_components/espressif__esp_hosted)
set(TAFEL_EH_SDIO ${TAFEL_EH_DIR}/host/mcu/eh_host_mcu_transport/src/eh_host_bus_sdio.c)
if(EXISTS ${TAFEL_EH_SDIO})
    file(READ ${TAFEL_EH_SDIO} TAFEL_EH_QUELLE)
    string(FIND "${TAFEL_EH_QUELLE}" "TAB5_RXBUF_PATCH" TAFEL_EH_DRIN)
    if(TAFEL_EH_DRIN EQUAL -1)
        execute_process(COMMAND patch -p1 -N -i ${CMAKE_CURRENT_LIST_DIR}/patches/esp_hosted-3.0.9-rx-puffer.patch
                        WORKING_DIRECTORY ${TAFEL_EH_DIR}
                        RESULT_VARIABLE TAFEL_EH_RC OUTPUT_VARIABLE TAFEL_EH_AUS ERROR_VARIABLE TAFEL_EH_AUS)
        if(NOT TAFEL_EH_RC EQUAL 0)
            message(FATAL_ERROR "ESP-Hosted-Patch passt nicht:\n${TAFEL_EH_AUS}")
        endif()
        message(STATUS "ESP-Hosted: Patch für die SDIO-Lesepuffer eingespielt")
    endif()
endif()
