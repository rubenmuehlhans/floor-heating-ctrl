# Vorschau der Bedientafel auf dem Mac (host/vorschau.sh).
# LVGL und cJSON kommen aus managed_components (einmal `idf.py reconfigure`).
cmake_minimum_required(VERSION 3.16)
project(tafel_vorschau C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_BUILD_TYPE Debug)
set(T ${CMAKE_CURRENT_LIST_DIR}/..)
set(LV ${T}/managed_components/lvgl__lvgl)
set(CJ ${T}/managed_components/espressif__cjson/cJSON)

file(GLOB_RECURSE LV_SRCS ${LV}/src/*.c)
add_library(lvgl STATIC ${LV_SRCS})
target_include_directories(lvgl PUBLIC ${LV} ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE=1)
target_compile_options(lvgl PRIVATE -w -O2)

file(GLOB KERN ${T}/main/kern/*.cpp)
file(GLOB UI ${T}/main/ui/*.cpp)
file(GLOB GEN ${T}/main/gen/*.c)
add_executable(vorschau vorschau.cpp plattform.cpp ${KERN} ${UI} ${GEN} ${CJ}/cJSON.c)
target_include_directories(vorschau PRIVATE ${T}/main ${T}/main/kern ${T}/main/ui ${T}/main/gen ${CJ})
target_compile_options(vorschau PRIVATE -Wall -Wno-unused-function -Wno-deprecated-declarations -Wno-missing-field-initializers)
target_link_libraries(vorschau lvgl z)
