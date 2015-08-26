CC=gcc

TDA_FILES=src/tda.c
TDA_TARGET=tda
TDA_VERSION=${shell git rev-parse --short HEAD}
TDA_CFLAGS=-DTDA_VERSION=\"${TDA_VERSION}\"

.PHONY: tda all clean

tda:
	${CC} ${TDA_CFLAGS} ${TDA_FILES} -o ${TDA_TARGET}

all:
	tda

clean:
	rm ${TDA_TARGET}
