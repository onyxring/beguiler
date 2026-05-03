CXX      ?= c++
CXXFLAGS ?= -std=c++20 -O2 -Wno-deprecated-declarations
TARGET    = beguiler
SOURCES   = $(wildcard *.cpp)

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

debug: CXXFLAGS = -std=c++20 -g -O0
debug: $(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
	rm -rf $(TARGET).dSYM

.PHONY: all debug clean
