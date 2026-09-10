CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude -pthread

SRCS = src/stl_order_book.cpp src/pool_order_book.cpp src/protocol.cpp src/tcp_server.cpp
OBJS = $(SRCS:.cpp=.o)

BIN_DIR = bin
SERVER_BIN = $(BIN_DIR)/titan_server
TEST_STL_BIN = $(BIN_DIR)/test_stl_order_book
TEST_DIFF_BIN = $(BIN_DIR)/test_differential
TEST_GATEWAY_BIN = $(BIN_DIR)/test_gateway
TEST_SPSC_BIN = $(BIN_DIR)/test_spsc_queue
TEST_CONCURRENT_BIN = $(BIN_DIR)/test_concurrent_gateway
BENCHMARK_BIN = $(BIN_DIR)/benchmark_engine
BENCHMARK_SPSC_BIN = $(BIN_DIR)/benchmark_spsc

all: $(BIN_DIR) $(SERVER_BIN) $(TEST_STL_BIN) $(TEST_DIFF_BIN) $(TEST_GATEWAY_BIN) $(TEST_SPSC_BIN) $(TEST_CONCURRENT_BIN) $(BENCHMARK_BIN) $(BENCHMARK_SPSC_BIN)

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

$(TEST_SPSC_BIN): $(BIN_DIR) tests/test_spsc_queue.cpp
	$(CXX) $(CXXFLAGS) tests/test_spsc_queue.cpp -o $@

$(TEST_CONCURRENT_BIN): $(BIN_DIR) $(OBJS) tests/test_concurrent_gateway.cpp
	$(CXX) $(CXXFLAGS) tests/test_concurrent_gateway.cpp $(OBJS) -o $@

$(BENCHMARK_BIN): $(BIN_DIR) $(OBJS) benchmarks/benchmark_engine.cpp
	$(CXX) $(CXXFLAGS) benchmarks/benchmark_engine.cpp $(OBJS) -o $@

$(BENCHMARK_SPSC_BIN): $(BIN_DIR) $(OBJS) benchmarks/benchmark_spsc.cpp
	$(CXX) $(CXXFLAGS) benchmarks/benchmark_spsc.cpp $(OBJS) -o $@

test: $(TEST_STL_BIN) $(TEST_DIFF_BIN) $(TEST_GATEWAY_BIN) $(TEST_SPSC_BIN) $(TEST_CONCURRENT_BIN)
	@echo "\n>>> 1. Running STL Unit Tests..."
	./$(TEST_STL_BIN)
	@echo "\n>>> 2. Running Differential & Fuzzing Tests (Stl vs Pool)..."
	./$(TEST_DIFF_BIN)
	@echo "\n>>> 3. Running TCP Order Gateway & Socket Integration Tests..."
	./$(TEST_GATEWAY_BIN)
	@echo "\n>>> 4. Running Lock-Free SPSC Queue Tests..."
	./$(TEST_SPSC_BIN)
	@echo "\n>>> 5. Running Concurrent Ingress Multi-Client Tests..."
	./$(TEST_CONCURRENT_BIN)

bench: $(BENCHMARK_BIN)
	./$(BENCHMARK_BIN) 200000

bench_spsc: $(BENCHMARK_SPSC_BIN)
	./$(BENCHMARK_SPSC_BIN) 200000

server: $(SERVER_BIN)
	./$(SERVER_BIN) 8080

clean:
	rm -rf $(BIN_DIR) src/*.o

.PHONY: all test bench bench_spsc server clean
