CC := gcc
CPPFLAGS := -Iinclude -D_POSIX_C_SOURCE=200809L
CFLAGS := -std=c99 -Wall -Wextra -Wpedantic -O3 -MMD -MP
LDLIBS := -lm
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)
DEP := $(OBJ:.o=.d)
TARGET := raytracer

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEP)

asan:
	$(CC) $(CPPFLAGS) -std=c99 -Wall -Wextra -Wpedantic -g -O1 -fsanitize=address,undefined $(SRC) -o $(TARGET)_asan $(LDLIBS)

run_p2: $(TARGET)
	mkdir -p images
	./$(TARGET) scenes/p2.scene --mode flat --output images/p2.avs

run_p3_1: $(TARGET)
	mkdir -p images
	./$(TARGET) scenes/P3_1.scene --mode phong --output images/p3_1.avs

run_p3_2: $(TARGET)
	mkdir -p images
	./$(TARGET) scenes/P3_2.scene --mode phong --output images/p3_2.avs

run_p4_1: $(TARGET)
	mkdir -p images
	./$(TARGET) scenes/P4_1.scene --mode phong --output images/P4_1.avs

run_p4_2: $(TARGET)
	mkdir -p images
	./$(TARGET) scenes/P4_2.scene --mode phong --output images/P4_2.avs

clean:
	rm -f $(OBJ) $(DEP) $(TARGET) $(TARGET)_asan

.PHONY: all asan clean run_p2 run_p3_1 run_p3_2 run_p4_1 run_p4_2
