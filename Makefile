# Executable name
TARGET = neko_installer

# Compiler and flags
CC     = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0` -Iinclude
LIBS   = `pkg-config --libs gtk+-3.0` -lpthread
DESKTOP_FLAG = -DHAS_DESKTOP_TAB

# -----------------------------------------------------------------------
# Shared source files (both builds)
# -----------------------------------------------------------------------
SRCS_COMMON = src/core/main.c \
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

# -----------------------------------------------------------------------
# Exclusive Void Linux module (xbps, repo keys, xbps-reconfigure...)
# ONLY included in the normal build.
# -----------------------------------------------------------------------
SRCS_VOID = src/core/installer_steps_void.c

# Desktop setup module (normal build only)
SRCS_DESKTOP = src/core/desktops-setup.c

# Build normal: common + Void module + Desktop
SRCS = $(SRCS_COMMON) $(SRCS_VOID) $(SRCS_DESKTOP)
OBJS = $(SRCS:.c=.o)

# Build universal: objects in build/universal/ to avoid mixing flags
BUILDDIR_UNI = build/universal
OBJS_UNI     = $(addprefix $(BUILDDIR_UNI)/,$(notdir $(SRCS_COMMON:.c=.o)))

# -----------------------------------------------------------------------
# Reglas
# -----------------------------------------------------------------------
all: $(TARGET)

# --- Normal build (Void Linux) ---
$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)

# Compile .c -> .o (normal build, without -DUNIVERSAL_BUILD)
%.o: %.c include/neko_installer.h include/lang.h include/country_data.h
	$(CC) $(CFLAGS) $(DESKTOP_FLAG) -c $< -o $@

# --- Universal build (without xbps / Void Linux) ---
$(BUILDDIR_UNI):
	mkdir -p $(BUILDDIR_UNI)

# Generic rule for universal objects (searches for .c in all subfolders)
$(BUILDDIR_UNI)/%.o: src/core/%.c include/neko_installer.h include/lang.h include/country_data.h | $(BUILDDIR_UNI)
	$(CC) $(CFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

$(BUILDDIR_UNI)/%.o: src/ui/%.c include/neko_installer.h include/lang.h include/country_data.h | $(BUILDDIR_UNI)
	$(CC) $(CFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

$(BUILDDIR_UNI)/%.o: src/i18n/%.c include/neko_installer.h include/lang.h include/country_data.h | $(BUILDDIR_UNI)
	$(CC) $(CFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

universal: $(OBJS_UNI)
	$(CC) $(OBJS_UNI) -o $(TARGET)_universal $(LIBS)
	@echo ""
	@echo ">>> Universal build ready: $(TARGET)_universal"
	@echo "    (No XBPS / Void Linux steps)"

# Clean
clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)_universal
	rm -rf $(BUILDDIR_UNI)

# Run (requires sudo to mount disks)
run: all
	sudo ./$(TARGET)

run-universal: universal
	sudo ./$(TARGET)_universal
