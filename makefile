CC = gcc

PROGS = servidor cliente

all: ${PROGS}

servidor: servidor.o
	${CC} -o $@ servidor.o

cliente: cliente.o
	${CC} -o $@ cliente.o