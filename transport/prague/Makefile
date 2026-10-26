# Builds the Prague binding: the reference implementation plus the shim, as one
# shared object that ctypes can load.
#
# The reference source is compiled here rather than linking the `libprague.a` that its
# own Makefile produces, because that archive is built without -fPIC and cannot go into
# a shared object. Nothing in the L4S tree is modified.

# In the development workspace the L4S reference tree is a sibling of the FikoRE
# repository. A standalone checkout can point this at its own copy.
PRAGUE_DIR ?= ../../../L4STeam/udp_prague

CXX      ?= g++
CXXFLAGS := -std=c++11 -O3 -fPIC -Wall -Wextra -I$(PRAGUE_DIR)

TARGET := libpraguesim.so
SOURCES := prague_shim.cpp $(PRAGUE_DIR)/prague_cc.cpp

all: $(TARGET)

$(TARGET): $(SOURCES) $(PRAGUE_DIR)/prague_cc.h Makefile
	$(CXX) $(CXXFLAGS) -shared $(SOURCES) -o $@

clean:
	rm -f $(TARGET)

.PHONY: all clean
