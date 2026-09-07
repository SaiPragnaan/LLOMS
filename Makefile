CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -Wpedantic -Iinclude

SRC = src/stl_order_book.cpp
OBJ = $(SRC:.cpp=.o)

BIN_DIR = bin
TEST_BIN = $(BIN_DIR)/test_stl_order_book

all: $(TEST_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BIN): $(BIN_DIR) $(OBJ) tests/test_stl_order_book.cpp
	$(CXX) $(CXXFLAGS) tests/test_stl_order_book.cpp $(OBJ) -o $@

test: $(TEST_BIN)
	@echo "Running tests..."
	./$(TEST_BIN)

clean:
	rm -rf $(BIN_DIR) src/*.o

.PHONY: all test clean

