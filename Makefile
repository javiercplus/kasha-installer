# Executable name
TARGET = neko_installer

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

# Source files and objects
SRCS = main.c ui.c utils.c installer.c
OBJS = $(SRCS:.c=.o)

# Main rule
all: $(TARGET)

# Link objects to create the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compile .c files to .o
%.o: %.c neko_installer.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean temporary files
clean:
	rm -f $(OBJS) $(TARGET)

# Run the installer (requires sudo to mount disks)
run: all
	sudo ./$(TARGET)