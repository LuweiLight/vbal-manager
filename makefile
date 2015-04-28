CXX=g++
CXXFLAGS=-g -std=c++0x -Wall

all: vbal-manager

vbal-manager: vbal-manager.cc
	$(CXX) $(CXXFLAGS) vbal-manager.cc -o vbal-manager
clean:
	rm vbal-manager
