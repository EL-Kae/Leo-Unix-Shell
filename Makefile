# My make file 

FLAGS = -std=c99 -Wall -O1

all: Shell

Shell: Shell.c
	gcc ${FLAGS} -o Shell Shell.c

clean:
	rm -f Shell
