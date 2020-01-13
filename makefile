
macosx:
	make zxml.so "CC = clang" "DLLFLAGS = -undefined dynamic_lookup --shared"

linux:
	make zxml.so "CC = gcc" "DLLFLAGS = -shared -fPIC"

win: zxml.dll

zxml.dll: zxml_parser.c zxml.h lua_zxml.c
	gcc -g -Wall -O2 --shared -o $@ zxml_parser.c lua_zxml.c -I/usr/local/include -L/usr/local/bin -llua53

zxml.so: zxml_parser.c zxml.h lua_zxml.c
	$(CC) -g -Wall -O2 $(DLLFLAGS) -o $@  zxml_parser.c lua_zxml.c

test_zxml: zxml_parser.c zxml.h zxml_test.c
	clang -g -Wall -O2 -DZXML_TEST -o test_zxml zxml_parser.c zxml_test.c


.PHONY: macosx linux win

clean:
	rm -rf zxml.so test_zxml test_rxml