
zxml.so: zxml_parser.c zxml.h lua_zxml.c
	clang -g -Wall -undefined dynamic_lookup --shared -o $@  zxml_parser.c lua_zxml.c

test_zxml: zxml_parser.c zxml.h zxml_test.c
	clang -g -Wall -DZXML_TEST -o test_zxml zxml_parser.c zxml_test.c

test_rxml: rapidxml_test.cpp
	clang++ -g -Wall -O2 -o test_rxml rapidxml_test.cpp


clean:
	rm -rf zxml.so test_zxml test_rxml