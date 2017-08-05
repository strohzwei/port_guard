### .........  MAKEFILE ................................####
###		CFLAGS											####
###			-Wall		:gibt alle Warnings an			####
###			-pedantic	:code strict nach ISO C			####
###			-Werror		:macht alle Warnings zu Errors	####
###			-g			:enable Debug-Informationen		####
###														####
###		LDFLAGS											####
###			-lpthread	:wird fuer phtreads zum linken	####
###						benoetigt						####
###			-lm			:mathlib						####
###			-lrt		:real-time extensions			####
### .........  MAKEFILE ................................####

#CC=arm-cortexa8-linux-gnueabihf-gcc
CC=gcc
CFLAGS=-Wall -g -Werror --std=c11  
LDFLAGS=-lm -lpthread -lrt 

#loeschen
RM=rm -fr

HEADERS=$(wildcard *.h)
SOURCES=$(wildcard *.c)
TARGETS=$(SOURCES:.c=)
OBJECTS=$(SOURCES:.c=.o)

.PHONY: port_guard clean

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

port_guard: $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	$(RM) $(OBJECTS) $(TARGETS) *.so



