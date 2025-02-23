CC = gcc
CFLAGS = -Wall -Wextra -pedantic -O3 -march=native -mtune=native -funroll-loops -flto -fomit-frame-pointer -ffast-math -fno-math-errno -ftree-vectorize -fopenmp -pthread
LDFLAGS = -lSDL3 -lm
TARGET = mandelbrot
SRCS = mandelbrot.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(TARGET)

rebuild: clean all
