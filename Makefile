.PHONY: all

all: fast_procons

fast_procons: fast_procons.cpp
		g++ -static-libstdc++ -static-libgcc  -pthread -std=c++11 -g fast_procons.cpp -o fast_procons.o


