# Author: Andrei Pall
SOURCES = main.c
OBJS    = ${SOURCES:.c=.o}
CFLAGS  = `pkg-config gtk+-3.0 libcurl json-glib-1.0 --cflags`
LDADD   = `pkg-config gtk+-3.0 libcurl json-glib-1.0 --libs`
CC      = gcc
BIN     = google_drive_sync

all: ${OBJS}
	${CC} -Wall -no-pie -o ${BIN} ${OBJS} ${LDADD}
#	${CC} -g -no-pie -o ${BIN} ${OBJS} ${LDADD}
.PHONY: all

.c.o:
	${CC} ${CFLAGS} -c $<
#	${CC} ${CFLAGS} -g -c $<
.PHONY: clean
clean:
	rm -f *.o *~ core $(BIN)
# end of file
