CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude -pthread

SRCS = src/stl_order_book.cpp src/pool_order_book.cpp
SRCS = src/stl_order_book.cpp src/pool_order_book.cpp src/protocol.cpp src/tcp_server.cpp
OBJS = $(SRCS:.cpp=.o)

BIN_DIR = bin
SERVER_BIN = $(BIN_DIR)/titan_server
TEST_STL_BIN = $(BIN_DIR)/test_stl_order_book
TEST_DIFF_BIN = $(BIN_DIR)/test_differential
TEST_GATEWAY_BIN = $(BIN_DIR)/test_gateway
BENCHMARK_BIN = $(BIN_DIR)/benchmark_engine

all: $(BIN_DIR) $(TEST_STL_BIN) $(TEST_DIFF_BIN) $(BENCHMARK_BIN)
all: $(BIN_DIR) $(SERVER_BIN) $(TEST_STL_BIN) $(TEST_DIFF_BIN) $(TEST_GATEWAY_BIN) $(BENCHMARK_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SERVER_BIN): $(BIN_DIR) $(OBJS) src/main.cpp
	$(CXX) $(CXXFLAGS) src/main.cpp $(OBJS) -o $@

$(TEST_STL_BIN): $(BIN_DIR) src/stl_order_book.o tests/test_stl_order_book.cpp
	$(CXX) $(CXXFLAGS) tests/test_stl_order_book.cpp src/stl_order_book.o -o $@

$(TEST_DIFF_BIN): $(BIN_DIR) $(OBJS) tests/test_differential.cpp
	$(CXX) $(CXXFLAGS) tests/test_differential.cpp $(OBJS) -o $@

$(TEST_GATEWAY_BIN): $(BIN_DIR) $(OBJS) tests/test_gateway.cpp
	$(CXX) $(CXXFLAGS) tests/test_gateway.cpp $(OBJS) -o $@

$(BENCHMARK_BIN): $(BIN_DIR) $(OBJS) benchmarks/benchmark_engine.cpp
	$(CXX) $(CXXFLAGS) benchmarks/benchmark_engine.cpp $(OBJS) -o $@

test: $(TEST_STL_BIN) $(TEST_DIFF_BIN)
test: $(TEST_STL_BIN) $(TEST_DIFF_BIN) $(TEST_GATEWAY_BIN)
	@echo "\n>>> Running STL Unit Tests..."
	./$(TEST_STL_BIN)
	@echo "\n>>> Running Differential & Fuzzing Tests (Stl vs Pool)..."
	./$(TEST_DIFF_BIN)
	@echo "\n>>> Running TCP Order Gateway & Socket Integration Tests..."
	./$(TEST_GATEWAY_BIN)

bench: $(BENCHMARK_BIN)
	./$(BENCHMARK_BIN) 200000

server: $(SERVER_BIN)
	./$(SERVER_BIN) 8080

clean:
	rm -rf $(BIN_DIR) src/*.o

.PHONY: all test bench clean
.PHONY: all test bench server clean
