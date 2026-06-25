CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -g -Iinclude
TARGET := AVServer
SOURCES := src/main.cpp src/AVServer.cpp src/MediaManager.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)
