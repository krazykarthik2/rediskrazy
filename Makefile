CC = gcc
CFLAGS = -O2 -Wall -Wextra -fPIC
LDFLAGS = -pthread -lm

OUTPUT_DIR = execs
BACKEND_DIR = src_c/backend
QP_DIR = src_c/query_processor
UI_DIR = src_c/UI

SERVER_SRCS = $(BACKEND_DIR)/dict.c $(BACKEND_DIR)/rdb.c $(BACKEND_DIR)/ae.c \
              $(BACKEND_DIR)/resp.c $(BACKEND_DIR)/avl.c $(BACKEND_DIR)/zset.c \
              $(BACKEND_DIR)/tpool.c $(BACKEND_DIR)/sds.c $(BACKEND_DIR)/mempool.c \
              $(BACKEND_DIR)/expheap.c $(BACKEND_DIR)/aofbuf.c $(BACKEND_DIR)/server.c

QP_SRCS = $(QP_DIR)/sql_parser.c $(QP_DIR)/sql_lexer.c $(QP_DIR)/sql_parser_internal.c \
          $(QP_DIR)/sql_translator.c $(QP_DIR)/schema_manager.c

TARGETS = $(OUTPUT_DIR)/server $(OUTPUT_DIR)/qp_server $(OUTPUT_DIR)/sql_cli

all: $(OUTPUT_DIR) $(TARGETS)

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

$(OUTPUT_DIR)/server: $(SERVER_SRCS)
	$(CC) $(CFLAGS) $(SERVER_SRCS) -o $@ $(LDFLAGS)

$(OUTPUT_DIR)/qp_server: $(QP_SRCS) $(QP_DIR)/qp_server.c $(BACKEND_DIR)/sds.c
	$(CC) $(CFLAGS) $^ -o $@

$(OUTPUT_DIR)/sql_cli: $(QP_SRCS) $(UI_DIR)/table_formatter.c $(UI_DIR)/cli.c $(BACKEND_DIR)/sds.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGETS)
