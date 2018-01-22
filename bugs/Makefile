CFLAGS	= -I .. 

all:b a404 a200
	
a404:main.cpp unqlite-modify.o
	g++ $(CFLAGS) -D TEST_COMMIT $^ -o $@

a200:main.cpp unqlite-modify.o
	g++ $(CFLAGS) $^ -o $@

b:main.cpp unqlite-org.o
	g++ $(CFLAGS) -D TEST_COMMIT $^ -o $@

test:
	a404 > 404
	a200 > 200

unqlite-modify.o:../unqlite-modify.c
	gcc $(CFLAGS) -c $< -o $@

unqlite-org.o:../unqlite.c
	gcc $(CFLAGS) -c $< -o $@

