CC=gcc

TDA_FILES=src/tda.c
TDA_TARGET=tda

.PHONY: tda all clean

tda:
	${CC} ${TDA_FILES} -o ${TDA_TARGET}

all:
	tda

clean:
	rm ${TDA_TARGET}
