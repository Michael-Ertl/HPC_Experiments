
# AI MAKEFILE

# 1. Name of your final executable
TARGET = build/vrp_solver

# 2. Compiler and basic flags
CC = gcc
CFLAGS = -Wall -Wextra -g

# 3. Automatically find all .c files in the CURRENT directory
SRCS = $(wildcard src/*.c)

# 4. Create a list of .o files based on the .c files found
OBJS = $(SRCS:.c=.o)

# Default rule
all: $(TARGET)

# Link the object files to create the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# Compile each .c file into a .o file
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Clean up the directory
clean:
	rm -f *.o $(TARGET)

.PHONY: all clean