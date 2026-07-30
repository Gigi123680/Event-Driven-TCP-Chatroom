CXX := g++
CC := gcc

TARGET := chat
BUILD_DIR := build
SRC_DIR := src

CXXFLAGS := -std=c++17 -Wall -Wextra -pedantic
CFLAGS := -Wall -Wextra -pedantic

CPP_SOURCES := \
	$(SRC_DIR)/main.cpp \
	$(SRC_DIR)/client/client.cpp \
	$(SRC_DIR)/server/server.cpp \
	$(SRC_DIR)/connection/connection.cpp \
	$(SRC_DIR)/network/epoll.cpp \
	$(SRC_DIR)/network/protocol.cpp \
	$(SRC_DIR)/frontend/formatPrint.cpp

C_SOURCES := \
	$(SRC_DIR)/log/log.c

CPP_OBJECTS := $(CPP_SOURCES:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/%.o)
C_OBJECTS := $(C_SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
OBJECTS := $(CPP_OBJECTS) $(C_OBJECTS)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
