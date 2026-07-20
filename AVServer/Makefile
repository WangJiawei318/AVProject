CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -Wpedantic -g -Iinclude
TARGET := AVServer
SOURCES := src/main.cpp src/AVServer.cpp src/EpollServer.cpp \
	src/ConnectionContext.cpp src/ProtocolDispatcher.cpp \
	src/MediaManager.cpp src/UploadManager.cpp src/DownloadManager.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)
