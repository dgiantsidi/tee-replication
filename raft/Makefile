CXX = g++
CXX_FLAGS = -g -O3 -s -Wall -std=c++20 -Wno-unused-function -Wno-unused-value -Wno-return-type -fsanitize=address
LD_FLAGS = -lfmt -pthread

SRC_FILES = net/shared.cpp

all : clean leader follower

clean:
	rm -f leader follower

leader:
	$(CXX) $(CXX_FLAGS) $@.cpp $(SRC_FILES) -o $@ $(LD_FLAGS)


follower:
	$(CXX) $(CXX_FLAGS) $@.cpp $(SRC_FILES) -o $@ $(LD_FLAGS)
