CC = clang
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

all: tftp.out

tftp.out: hw2.c
	$(CC) $(CFLAGS) -o $@ hw2.c $(LDFLAGS)

clean:
	rm -f tftp.out *.o

