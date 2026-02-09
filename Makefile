# Nombre del ejecutable
TARGET = neko_installer

# Compilador y banderas
CC = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0`
LIBS = `pkg-config --libs gtk+-3.0` -lpthread

# Archivos fuente y objetos
SRCS = main.c ui.c utils.c installer.c
OBJS = $(SRCS:.c=.o)

# Regla principal
all: $(TARGET)

# Vincular los objetos para crear el ejecutable
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compilar archivos .c a .o
%.o: %.c neko_installer.h
	$(CC) $(CFLAGS) -c $< -o $@

# Limpiar archivos temporales
clean:
	rm -f $(OBJS) $(TARGET)

# Ejecutar el instalador (requiere sudo para montar discos)
run: all
	sudo ./$(TARGET)