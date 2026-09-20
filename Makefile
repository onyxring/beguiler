# Per-object build with compiler-generated header dependencies.
#
# -MMD -MP records header dependencies per object so a header edit rebuilds dependents;
# without them a .h is absent from the dependency graph and `make` keeps the stale binary.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wno-deprecated-declarations
TARGET    = beguiler
SOURCES   = $(wildcard *.cpp)
BUILDDIR  = build
OBJECTS   = $(SOURCES:%.cpp=$(BUILDDIR)/%.o)
DEPS      = $(OBJECTS:.o=.d)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $(TARGET)

$(BUILDDIR)/%.o: %.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# The .d files are generated as a side effect of compiling, so they are absent on a
# clean tree — hence -include rather than include.
-include $(DEPS)

# Flags are not part of the dependency graph, so a debug build starts from clean;
# otherwise objects compiled -O2 would be linked into a -O0 binary.
debug:
	$(MAKE) clean
	$(MAKE) CXXFLAGS="-std=c++17 -g -O0" $(TARGET)

clean:
	rm -f $(TARGET) $(TARGET).exe
	rm -rf $(TARGET).dSYM $(BUILDDIR)

.PHONY: all debug clean
