
CXX = g++

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra \
           $(shell pkg-config --cflags jack sndfile)

LIBS = $(shell pkg-config --libs jack sndfile)

TARGET = drum_machine

all:
	$(CXX) $(CXXFLAGS) src/main.cpp -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
