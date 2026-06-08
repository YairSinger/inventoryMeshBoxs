UNITY = vendor/unity
CC    = cc
CFLAGS = -std=c99 -Wall -Wextra \
         -I$(UNITY) \
         -I../include

SRCS = main/test_imb_buzzer.c \
       main/runner.c \
       ../imb_buzzer.c \
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
