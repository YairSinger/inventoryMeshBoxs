UNITY = ../../imb_buzzer/test/vendor/unity
TYPES = ../../imb_types/include
DETECTOR = ../../imb_detector/include
CC    = cc
CFLAGS = -std=c99 -Wall -Wextra \
         -I$(UNITY) \
         -I../include \
         -I$(TYPES) \
         -I$(DETECTOR)

SRCS = main/test_imb_display.c \
       main/runner.c \
       ../imb_display.c \
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
