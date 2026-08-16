# Prueft Regelgesetz, Ventil-Zustandsmaschine, Hardwarezuordnung und
# Pumpensteuerung auf dem Rechner. Braucht weder ESP-IDF noch Hardware.
#
#   make -C test/host

ROOT := ../..
CFLAGS := -std=c11 -Wall -Wextra -Werror -O1 -g \
          -I$(ROOT)/components/hw_map/include \
          -I$(ROOT)/components/valve/include \
          -I$(ROOT)/components/roomctrl/include \
          -I$(ROOT)/components/heatlogic/include

SRC := test_logic.c \
       $(ROOT)/components/hw_map/hw_map.c \
       $(ROOT)/components/valve/valve.c \
       $(ROOT)/components/roomctrl/roomctrl.c \
       $(ROOT)/components/heatlogic/heatlogic.c

all: run

test_logic: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC) -lm

run: test_logic
	./test_logic

clean:
	rm -f test_logic

.PHONY: all run clean
