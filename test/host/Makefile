# Prueft Regelgesetz, Ventil-Zustandsmaschine, Hardwarezuordnung,
# Pumpensteuerung, Terminplan und die Dekodierung der Funkpakete auf dem
# Rechner. Braucht weder ESP-IDF noch
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
       $(ROOT)/components/schedule/schedule.c \
       $(ROOT)/components/atc_ble/atc_decode.c

all: run

test_logic: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

run: test_logic
	./test_logic

clean:
	rm -f test_logic

.PHONY: all run clean
