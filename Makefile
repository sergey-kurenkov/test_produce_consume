.PHONY: all clean

all: fast_procons fixed_procons procons

fast_procons: fast_procons.cpp Makefile
		g++ $(CPPFLAGS) -static-libstdc++ -static-libgcc  -pthread -std=c++11 -g fast_procons.cpp -o fast_procons

fixed_procons: fixed_procons.cpp Makefile
		g++ $(CPPFLAGS) -static-libstdc++ -static-libgcc  -pthread -std=c++11 -g fixed_procons.cpp -o fixed_procons

procons: procons.cpp Makefile
		g++ $(CPPFLAGS) -static-libstdc++ -static-libgcc  -pthread -std=c++11 -g procons.cpp -o procons

clean:
		rm -fr fixed_procons fast_procons procons