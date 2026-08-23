# Executable name
TARGET = neko_installer

# Compiler and flags
CC     = gcc
CFLAGS = -Wall -Wextra -g `pkg-config --cflags gtk+-3.0` -Iinclude
LIBS   = `pkg-config --libs gtk+-3.0` -lpthread

# -----------------------------------------------------------------------
# Desktop tab.
#   make                      -> build WITH the desktop tab (default)
#   make NO_DESKTOP_TAB=1     -> build WITHOUT it. The installer then always
#                                copies the live image as-is ("local copy").
# -----------------------------------------------------------------------
ifneq ($(NO_DESKTOP_TAB),1)
DESKTOP_FLAG = -DHAS_DESKTOP_TAB
BUILD_CONFIG = desktop
else
BUILD_CONFIG = nodektop
endif

# -----------------------------------------------------------------------
# Shared source files (both builds)
# -----------------------------------------------------------------------
SRCS_COMMON = src/core/main.c \
              src/ui/build_ui.c \
              src/ui/ui_css.c \
              src/ui/ui_i18n.c \
              src/ui/ui_welcome.c \
              src/ui/ui_widgets.c \
              src/ui/ui_desktop.c \
              src/ui/ui_finished.c \
              src/core/utils.c \
              src/core/installer.c \
              src/core/partition_utils.c \
              src/core/country_data.c \
              src/ui/ui_partition.c \
              src/ui/ui_callbacks.c \
              src/core/hard_steps.c \
              src/core/final_step.c \
              src/core/firts_filesystems.c \
              src/core/step_partition.c \
              src/core/step_format.c \
              src/i18n/lang_manager.c \
              src/i18n/lang_en.c \
              src/i18n/lang_es.c \
              src/i18n/lang_ja.c

# -----------------------------------------------------------------------
# Exclusive Void Linux module (xbps, repo keys, xbps-reconfigure...)
# ONLY included in the normal build.
# -----------------------------------------------------------------------
SRCS_VOID = src/core/installer_steps_void.c

# Desktop setup module (normal build only, skipped when NO_DESKTOP_TAB=1)
SRCS_DESKTOP = src/core/desktops-setup.c

# Rootfs-based base install (normal build only, skipped when NO_DESKTOP_TAB=1)
SRCS_ROOTFS = src/core/rootfs-base.c

ifneq ($(NO_DESKTOP_TAB),1)
# Build normal: common + Void module + Desktop + Rootfs
SRCS = $(SRCS_COMMON) $(SRCS_VOID) $(SRCS_DESKTOP) $(SRCS_ROOTFS)
else
# Build without desktop: common + Void module only (local-copy install)
SRCS = $(SRCS_COMMON) $(SRCS_VOID)
endif
OBJS = $(SRCS:.c=.o)

# Build universal: objects in build/universal/ to avoid mixing flags
BUILDDIR_UNI = build/universal
OBJS_UNI     = $(addprefix $(BUILDDIR_UNI)/,$(notdir $(SRCS_COMMON:.c=.o)))

# -----------------------------------------------------------------------
# Reglas
# -----------------------------------------------------------------------

# Object files do NOT track compiler-flag changes on their own. Record the
# current build configuration in a stamp file so that toggling
# NO_DESKTOP_TAB (or any other flag below) forces a clean rebuild instead
# of reusing stale objects / a stale binary.
BUILD_STAMP = .build_config

all: $(BUILD_STAMP) $(TARGET)

$(BUILD_STAMP): FORCE
	@if [ -f "$@" ] && [ "$$(cat "$@")" != "$(BUILD_CONFIG)" ]; then \
		echo ">>> Build configuration changed ($(BUILD_CONFIG)), cleaning previous objects..."; \
		$(MAKE) clean; \
	fi
	@echo "$(BUILD_CONFIG)" > "$@"

.PHONY: FORCE
FORCE:

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
	rm -f $(TARGET) $(TARGET)_universal
	rm -f src/core/*.o src/ui/*.o src/i18n/*.o
	rm -f $(BUILD_STAMP)
	rm -rf $(BUILDDIR_UNI)

# Run (requires sudo to mount disks)
run: all
	sudo ./$(TARGET)

run-universal: universal
	sudo ./$(TARGET)_universal
