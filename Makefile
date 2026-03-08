CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -pthread -O2 -D_GNU_SOURCE
LDFLAGS = -pthread

SRC_DIR   = src
TEST_DIR  = tests
BUILD_DIR = build

SRCS = $(SRC_DIR)/lanes.c $(SRC_DIR)/parallel_sum.c $(SRC_DIR)/parallel_radix_sort.c
OBJS = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

.PHONY: all clean test run

all: $(BUILD_DIR)/main $(BUILD_DIR)/test_runner

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/main: $(SRC_DIR)/main.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_runner: $(TEST_DIR)/test_runner.c $(OBJS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

run: $(BUILD_DIR)/main
	./$(BUILD_DIR)/main

test: $(BUILD_DIR)/test_runner
	./$(BUILD_DIR)/test_runner $(TEST)

clean:
	rm -rf $(BUILD_DIR)
