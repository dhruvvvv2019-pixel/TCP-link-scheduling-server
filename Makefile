CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -pthread -Wall -Wextra -pedantic
CPPFLAGS ?= -Isrc

all: server client

server: src/server.cpp src/common.hpp src/config.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) src/server.cpp -o server

client: src/client.cpp src/common.hpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) src/client.cpp -o client

clean:
	rm -f server client metrics.csv results/*.csv results/*.png
