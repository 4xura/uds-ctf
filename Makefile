CC=gcc
CFLAGS=-Wall -O2 -fno-pie -no-pie -Wl,-Ttext=0x40000000
CPPFLAGS += -Ithird_party/iso14229
BUILD_DIR=build
OBJS=$(BUILD_DIR)/uds_server.o $(BUILD_DIR)/iso14229.o

.PHONY: all clean

all: $(BUILD_DIR)/uds_server

$(BUILD_DIR)/uds_server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD_DIR)/uds_server.o: src/uds_server.c third_party/iso14229/iso14229.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/iso14229.o: third_party/iso14229/iso14229.c third_party/iso14229/iso14229.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(OBJS) $(BUILD_DIR)/uds_server
