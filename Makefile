
CXX = g++

CXXFLAGS = -std=c++17 -O2 -Wall -Wextra \
 $(shell pkg-config --cflags jack sndfile) \
 $(shell sdl2-config --cflags)

LIBS = $(shell pkg-config --libs jack sndfile) \
 $(shell sdl2-config --libs)

TARGET = drum_machine

all:
	$(CXX) $(CXXFLAGS) src/main.cpp src/ui.cpp -o $(TARGET) $(LIBS)

clean:
	rm -f $(TARGET)
