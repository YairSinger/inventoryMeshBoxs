UNITY = vendor/unity
CC    = cc
CFLAGS = -std=c99 -Wall -Wextra \
         -I$(UNITY) \
         -I../include \
         -I../../imb_types/include \
         -I../../imb_detector/include \
         -I../../imb_session/include \
         -I../../imb_registry/include \
         -I../../imb_delta/include

SRCS = main/test_imb_protocol.c \
       main/runner.c \
       ../imb_protocol.c \
       $(UNITY)/unity.c

OUT = build/test_runner

.PHONY: all run clean

all: $(OUT)

$(OUT): $(SRCS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^

run: $(OUT)
	@./$(OUT) -v

clean:
	rm -rf build
