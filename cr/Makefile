CXX = g++
CXX_FLAGS = -g -O3 -s -Wall -std=c++20 -Wno-unused-function -Wno-unused-value -Wno-return-type -fsanitize=address
LD_FLAGS = -lfmt -pthread -lcrypto

SRC_FILES = net/shared.cpp

all : clean head middle tail

clean:
	rm -f  head middle tail

head:
	$(CXX) $(CXX_FLAGS) $@.cpp $(SRC_FILES) -o $@ $(LD_FLAGS)


middle:
	$(CXX) $(CXX_FLAGS) $@.cpp $(SRC_FILES) -o $@ $(LD_FLAGS)

tail:
	$(CXX) $(CXX_FLAGS) $@.cpp $(SRC_FILES) -o $@ $(LD_FLAGS)
