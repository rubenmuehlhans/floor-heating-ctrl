# Bedientafel — Quellen.
# ⚠️ Eine NEUE Datei in ui/, kern/, geraet/ oder gen/ sieht CMake erst nach
# `idf.py reconfigure` (GLOB beim Konfigurieren).
file(GLOB GEN_SRCS ${CMAKE_CURRENT_LIST_DIR}/gen/*.c)
file(GLOB UI_SRCS ${CMAKE_CURRENT_LIST_DIR}/ui/*.cpp)
file(GLOB KERN_SRCS ${CMAKE_CURRENT_LIST_DIR}/kern/*.cpp)
file(GLOB GERAET_SRCS ${CMAKE_CURRENT_LIST_DIR}/geraet/*.cpp)
set(DOCS ${CMAKE_CURRENT_LIST_DIR}/../../../docs)

idf_component_register(
    SRCS "main.cpp" ${GERAET_SRCS} ${KERN_SRCS} ${UI_SRCS} ${GEN_SRCS}
    INCLUDE_DIRS "." "gen" "ui" "kern" "geraet"
    # Das Wissen des Assistenten: Handbuch und Konzepte aus dem Repository, nicht kopiert
    # (wie apple/project.yml), dazu die Beispielanlage.
    EMBED_TXTFILES "${DOCS}/handbuch.md" "${DOCS}/konzept-waermeerzeuger.md" "${DOCS}/konzept-auswertung.md"
                   "beispiel/beispiel.json"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
