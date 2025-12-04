OPENSSL = openssl

GREEN := \033[32m
RED := \033[31m
YELLOW := \033[33m
RESET := \033[0m

CC = gcc
CFLAGS = -Wall -g
CFLAGS_CLIENT = -Wall -g $(shell pkg-config --cflags gtk+-3.0)
LIBS_SERVER = -lssl -lcrypto -pthread -lmysqlclient 
LIBS_CLIENT = -lssl -lcrypto -pthread $(shell pkg-config --libs gtk+-3.0)

INCLUDE_DIR = include
SERVER_DIR = src/server
CLIENT_DIR = src/client
SERVER_BUILD_DIR = build/server
CLIENT_BUILD_DIR = build/client
CERTS_DIR = certs
FILES_DIR = files
VIDEO_DIR = videos

SERVER_SRC = $(wildcard $(SERVER_DIR)/*.c)
CLIENT_SRC = $(wildcard $(CLIENT_DIR)/*.c)

SERVER_OBJ = $(SERVER_SRC:$(SERVER_DIR)/%.c=$(SERVER_BUILD_DIR)/%.o)
CLIENT_OBJ = $(CLIENT_SRC:$(CLIENT_DIR)/%.c=$(CLIENT_BUILD_DIR)/%.o)

all: $(SERVER_BUILD_DIR) $(CLIENT_BUILD_DIR) $(CERTS_DIR) $(FILES_DIR) $(VIDEO_DIR) generate_certs server client

$(SERVER_BUILD_DIR):
	@mkdir -p $(SERVER_BUILD_DIR)
	@echo "$(GREEN)[LOG] Server build directory created.$(RESET)"

$(CLIENT_BUILD_DIR):
	@mkdir -p $(CLIENT_BUILD_DIR)
	@echo "$(GREEN)[LOG] Client build directory created.$(RESET)"

$(CERTS_DIR):
	@mkdir -p $(CERTS_DIR)
	@echo "$(GREEN)[LOG] Certs directory created.$(RESET)"

$(FILES_DIR):
	@mkdir -p $(FILES_DIR)
	@echo "$(GREEN)[LOG] Files directory created.$(RESET)"

$(VIDEO_DIR):
	@mkdir -p $(VIDEO_DIR)
	@echo "$(GREEN)[LOG] Videos directory created.$(RESET)"

generate_certs: $(CERTS_DIR)
	@$(OPENSSL) req -x509 -newkey rsa:2048 \
		-keyout $(CERTS_DIR)/server.key \
		-out $(CERTS_DIR)/server.crt \
		-days 30 -nodes \
		-subj "/C=TW/ST=Taiwan/L=Taipei/O=NTU/OU=NTUCSIE/CN=yeoguanwei/emailAddress=b1190209@ntu.edu.tw" > /dev/null 2>&1
	@echo "$(GREEN)[LOG] Server OpenSSL key and certificate generated.$(RESET)"

server: $(SERVER_OBJ)
	@$(CC) $(CFLAGS) -I $(INCLUDE_DIR) -o server $(SERVER_OBJ) $(LIBS_SERVER)
	@echo "$(GREEN)[LOG] Server executable created.$(RESET)"

client: $(CLIENT_OBJ)
	@$(CC) $(CFLAGS_CLIENT) -I $(INCLUDE_DIR) -o client $(CLIENT_OBJ) $(LIBS_CLIENT)
	@echo "$(GREEN)[LOG] Client executable created.$(RESET)"

$(SERVER_BUILD_DIR)/%.o: $(SERVER_DIR)/%.c
	@$(CC) $(CFLAGS) -I $(INCLUDE_DIR) -c $< -o $@ 2>/dev/null

$(CLIENT_BUILD_DIR)/%.o: $(CLIENT_DIR)/%.c
	@$(CC) $(CFLAGS_CLIENT) -I $(INCLUDE_DIR) -c $< -o $@

.PHONY: clean
clean:
	@rm -f server client
	@echo "$(RED)[LOG] Server and client executables removed.$(RESET)"
	@rm -rf build
	@echo "$(RED)[LOG] Build directory removed.$(RESET)"
	@rm -rf certs
	@echo "$(RED)[LOG] Certs directory removed.$(RESET)"
	@rm -rf files
	@echo "$(RED)[LOG] Files directory removed.$(RESET)"
	@rm -rf videos
	@echo "$(RED)[LOG] Videos directory removed.$(RESET)"