# Executable name
TARGET = neko_installer

# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0` -Iinclude
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

# Source files
SRCS = src/core/main.c \
       src/ui/ui.c \
       src/core/utils.c \
       src/core/installer.c \
       src/core/partition_utils.c \
       src/core/country_data.c \
       src/ui/ui_partition.c \
       src/ui/ui_callbacks.c \
       src/core/installer_steps.c \
       src/i18n/lang_manager.c \
       src/i18n/lang_en.c \
       src/i18n/lang_es.c \
       src/i18n/lang_ja.c

OBJS = $(SRCS:.c=.o)

# Main rule
all: $(TARGET)

# Link objects to create the executable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compile .c files to .o
%.o: %.c include/neko_installer.h include/lang.h include/country_data.h
	$(CC) $(CFLAGS) -c $< -o $@

# Clean temporary files
clean:
	rm -f $(OBJS) $(TARGET)

# Run the installer (requires sudo to mount disks)
run: all
	sudo ./$(TARGET)
