# Prueft Regelgesetz, Ventil-Zustandsmaschine, Hardwarezuordnung,
# Pumpensteuerung, Terminplan, die Dekodierung der Funkpakete und das Protokoll
# des Leitstands auf dem Rechner. Braucht weder ESP-IDF noch
# Hardware.
#
#   make -C test/host

ROOT := ../..
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
          -I$(ROOT)/components/hw_map/include \
          -I$(ROOT)/components/valve/include \
          -I$(ROOT)/components/roomctrl/include \
          -I$(ROOT)/components/heatlogic/include \
          -I$(ROOT)/components/schedule/include \
          -I$(ROOT)/components/atc_ble/include

SRC := test_logic.c \
       $(ROOT)/components/hw_map/hw_map.c \
       $(ROOT)/components/valve/valve.c \
       $(ROOT)/components/roomctrl/roomctrl.c \
       $(ROOT)/components/heatlogic/heatlogic.c \
       $(ROOT)/components/heatlogic/plausi.c \
       $(ROOT)/components/heatlogic/boilerpump.c \
       $(ROOT)/components/heatlogic/trend.c \
       $(ROOT)/components/heatlogic/flue.c \
       $(ROOT)/components/heatlogic/zapfung.c \
       $(ROOT)/components/schedule/schedule.c \
       $(ROOT)/components/atc_ble/atc_decode.c \
       $(ROOT)/components/atc_ble/atc_crypto.c

# Das Protokoll des Leitstands braucht cJSON. Genommen wird die Fassung, die
# ESP-IDF 5.x mitbringt, sonst die des Leitstands aus dem Komponentenverwalter.
CJSON_DIR ?= $(firstword $(wildcard $(IDF_PATH)/components/json/cJSON \
                                    $(HOME)/esp/esp-idf-5.3.6/components/json/cJSON \
                                    $(ROOT)/apps/station/managed_components/espressif__cjson/cJSON))
PK_SRC := test_protokoll.c \
          $(ROOT)/components/protokoll/katalog.c \
          $(ROOT)/components/protokoll/reihe.c \
          $(CJSON_DIR)/cJSON.c

all: run

test_logic: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

test_protokoll: $(PK_SRC)
	@test -n "$(CJSON_DIR)" || (echo "cJSON nicht gefunden, CJSON_DIR setzen" && false)
	$(CC) $(CFLAGS) -I$(ROOT)/components/protokoll/include -I$(CJSON_DIR) -o $@ $(PK_SRC) -lm

run: test_logic test_protokoll
	./test_logic
	./test_protokoll

clean:
	rm -f test_logic test_protokoll

.PHONY: all run clean
