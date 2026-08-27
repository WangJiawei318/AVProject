CXX := g++
CXXFLAGS := -std=c++11 -Wall -Wextra -Wpedantic -g -pthread -Iinclude $$(mysql_config --cflags)
LDLIBS := $$(mysql_config --libs) -lsodium
TARGET := AVServer
SOURCES := src/main.cpp src/AVServer.cpp src/EpollServer.cpp \
	src/ConnectionContext.cpp src/ProtocolDispatcher.cpp \
	src/MediaManager.cpp src/UploadManager.cpp src/DownloadManager.cpp \
	src/ThreadPool.cpp src/DatabaseConnectionPool.cpp src/AuthService.cpp \
	src/MediaRepository.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDLIBS)

clean:
	rm -f $(TARGET)
