# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -Wall -Wextra -w -std=c++23 -O

# Include paths
INCLUDEDIRS = -I/usr/local/include -I./include
# PYINCLUDES = -I/usr/include/python3.12 -I/usr/include/python3.12  -fno-strict-overflow -Wsign-compare -DNDEBUG -g -O2 -Wall
# PYINCLUDES = -I/usr/include/python3.12 -I/usr/include/python3.12  -fno-strict-overflow -Wsign-compare
PYINCLUDES = $(shell python3-config --embed --cflags)
PYINCLUDES += -I./pybind11-3.0/include

# Library paths and libraries
LIBDIRS = -L/usr/local/lib -L./libs
LIBS = -lbenchmark -lpthread
#PYLIBS = -L/usr/lib/python3.12/config-3.12-x86_64-linux-gnu -L/usr/lib/x86_64-linux-gnu -lpython3.12 -ldl  -lm
PYLIBS = $(shell python3-config --embed --ldflags)

# Target executable name
TARGET = pyhook

# Source files
SRCS = $(wildcard *.cpp)

# Object files (replace .cpp with .o)
OBJS = $(SRCS:.cpp=.o)

# Default target
all: $(TARGET)

# Debug build
debug: CXXFLAGS += -DDEBUG -g
debug: CCFLAGS += -DDEBUG -g
debug: $(TARGET)

# Benchmark build
benchmark: CXXFLAGS += -DBENCHMARK_PYHOOK
benchmark: CCFLAGS += -DBENCHMARK_PYHOOK
benchmark: $(TARGET)

# Disable PYBINDINGS
disable_pybind: CXXFLAGS += -DDISABLE_PYBIND
disable_pybind: CCFLAGS += -DDISABLE_PYBIND
disable_pybind: PYINCLUDES = 
disable_pybind: PYLIBS = 
disable_pybind: $(TARGET)

# Benchmark with PYBINDINGS disabled
benchmark_disable_pybind: CXXFLAGS += -DBENCHMARK_PYHOOK -DDISABLE_PYBIND
benchmark_disable_pybind: CCFLAGS += -DBENCHMARK_PYHOOK -DDISABLE_PYBIND
benchmark_disable_pybind: PYINCLUDES = 
benchmark_disable_pybind: PYLIBS = 
benchmark_disable_pybind: $(TARGET)

# Rule to build the target executable
$(TARGET): $(OBJS)
	@echo "Build $@"
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LIBDIRS) $(LIBS) $(PYLIBS)

# Rule to build object files from source files
%.o: %.cpp
	@echo "Build $@"
	$(CXX) $(CXXFLAGS) $(INCLUDEDIRS) $(PYINCLUDES) -c $< -o $@

# Clean up build files
clean:
	rm -rf $(OBJS) $(TARGET) __pycache__

print-vars:
	@echo "PYINCLUDES = $(PYINCLUDES)"
	@echo "PYLIBS = $(PYLIBS)"

# Phony targets (not actual files)
.PHONY: all clean

