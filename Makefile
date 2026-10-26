CXX := g++
PYTHON ?= python3
API_PYTHON ?= $(if $(wildcard api/.venv/bin/python),api/.venv/bin/python,$(PYTHON))

BIN_DIR := bin
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
TEST_OBJ_DIR := $(BUILD_DIR)/tests
TOOLS_OBJ_DIR := $(BUILD_DIR)/tools

TARGET := $(BIN_DIR)/fikore
TOOLS_TARGETS := $(BIN_DIR)/udp_tos_probe $(BIN_DIR)/scheduler_family_benchmark

CPPFLAGS := -Iinclude
CXXFLAGS := -O3 -g -std=c++17 -pthread
DEPFLAGS := -MMD -MP
LDFLAGS :=
LDLIBS := -pthread -lmnl -lnetfilter_queue

SRC_DIR := src
TEST_DIR := tests

APP_SOURCES := main.cpp $(shell find $(SRC_DIR) -name '*.cpp' | sort)
APP_OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(APP_SOURCES))
APP_DEPS := $(APP_OBJECTS:.o=.d)

LIB_SOURCES := $(shell find $(SRC_DIR) -name '*.cpp' | sort)
LIB_OBJECTS := $(patsubst %.cpp,$(OBJ_DIR)/%.o,$(LIB_SOURCES))

TEST_SOURCES := $(shell find $(TEST_DIR) -name '*_test.cpp' 2>/dev/null | sort)
TEST_OBJECTS := $(patsubst $(TEST_DIR)/%.cpp,$(TEST_OBJ_DIR)/%.o,$(TEST_SOURCES))
TEST_BINS := $(patsubst $(TEST_DIR)/%_test.cpp,$(TEST_OBJ_DIR)/%_test,$(TEST_SOURCES))
TEST_DEPS := $(TEST_OBJECTS:.o=.d)
TOOLS_SOURCES := $(shell find tools -name '*.cpp' 2>/dev/null | sort)
TOOLS_OBJECTS := $(patsubst tools/%.cpp,$(TOOLS_OBJ_DIR)/%.o,$(TOOLS_SOURCES))
TOOLS_DEPS := $(TOOLS_OBJECTS:.o=.d)
TRANSPORT_TESTS := \
	transport/tests/test_backend.py \
	transport/tests/test_iperf.py \
	transport/tests/test_loopback_transfer.py \
	transport/tests/test_prague.py \
	transport/tests/test_scenario.py \
	transport/tests/test_transports.py

.PHONY: all tools run test test-api test-transport test-transport-integration test-nfqueue smoke clean print-objects
.SECONDARY: $(TEST_OBJECTS)

all: $(TARGET) $(TOOLS_TARGETS)

tools: $(TOOLS_TARGETS)

run: all
	./$(TARGET)

$(TARGET): $(APP_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%.o: $(TEST_DIR)/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(TEST_OBJ_DIR)/%_test: $(TEST_OBJ_DIR)/%_test.o $(LIB_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TOOLS_OBJ_DIR)/%.o: tools/%.cpp
	@mkdir -p $(@D)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

$(BIN_DIR)/udp_tos_probe: $(TOOLS_OBJ_DIR)/udp_tos_probe.o
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ -o $@

$(BIN_DIR)/scheduler_family_benchmark: $(TOOLS_OBJ_DIR)/scheduler_family_benchmark.o $(LIB_OBJECTS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TARGET) $(TEST_BINS)
	@set -e; \
	for test_bin in $(TEST_BINS); do \
		echo "[TEST] $$test_bin"; \
		$$test_bin; \
	done
	@node_bin=$$(command -v nodejs || command -v node || true); \
	if [ -n "$$node_bin" ]; then \
		echo "[TEST] api/tests/dash_logic_test.js"; \
		$$node_bin api/tests/dash_logic_test.js > /dev/null; \
	else \
		echo "[SKIP] api/tests/dash_logic_test.js (no node interpreter)"; \
	fi
	@echo "[TEST] tests/exit_status_test.py"
	@$(PYTHON) tests/exit_status_test.py
	@echo "[TEST] tools/maps/validate_maps.py"
	@python3 tools/maps/validate_maps.py
	@echo "[TEST] tools/maps/test_generator_v2.py"
	@python3 tools/maps/test_generator_v2.py
	@echo "[TEST] tools/analyze_legacy_bler_table.py"
	@python3 tools/analyze_legacy_bler_table.py --expect-sha256 d61acbe2a5cea399570c53b40f0374261ca28ac80ccefed0aee85728ea9bda70 > /dev/null

test-transport:
	@set -e; \
	for test_file in $(TRANSPORT_TESTS); do \
		echo "[TEST] $$test_file"; \
		PYTHONPATH=transport $(PYTHON) $$test_file; \
	done

test-transport-integration: all
	@echo "[TEST] transport/tests/test_fikore_link.py"
	@PYTHONPATH=transport $(PYTHON) transport/tests/test_fikore_link.py
	@echo "[TEST] transport/tests/test_iperf_fikore.py"
	@PYTHONPATH=transport $(PYTHON) transport/tests/test_iperf_fikore.py

test-nfqueue: all
	@echo "[TEST] tests/nfqueue_namespace_test.py"
	@PYTHONPATH=. $(PYTHON) tests/nfqueue_namespace_test.py

test-api:
	@echo "[TEST] api/tests"
	@PYTHONPATH=api $(API_PYTHON) -m pytest api/tests -q

smoke: all
	./$(TARGET) tests/smoke_sim.ini
	./$(TARGET) tests/control_smoke.ini

clean:
	$(RM) -r $(BUILD_DIR) $(TARGET) $(TOOLS_TARGETS)

print-objects:
	@printf '%s\n' $(APP_OBJECTS)

-include $(APP_DEPS) $(TEST_DEPS) $(TOOLS_DEPS)
