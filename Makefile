# Executable name
TARGET = neko_installer

# Toolchain
CC  = gcc
CXX = g++

# wxWidgets config (fall back to wx-config-gtk3 when the `wx-config`
# alternative symlink is not installed, as on some distros)
WX_CONFIG ?= $(shell command -v wx-config 2>/dev/null || command -v wx-config-gtk3 2>/dev/null || echo wx-config)

# Flags
GLIB_CFLAGS = `pkg-config --cflags glib-2.0`
GLIB_LIBS   = `pkg-config --libs glib-2.0`
WX_CXXFLAGS = `$(WX_CONFIG) --cxxflags`
WX_LIBS     = `$(WX_CONFIG) --libs`

CFLAGS   = -Wall -Wextra -g $(GLIB_CFLAGS) -Iinclude -MMD -MP
CXXFLAGS = -std=c++14 -Wall -Wextra -g $(WX_CXXFLAGS) $(GLIB_CFLAGS) -Iinclude -MMD -MP
LIBS     = $(WX_LIBS) $(GLIB_LIBS) -lpthread

# -----------------------------------------------------------------------
# Desktop tab.
#   make                      -> build WITH the desktop tab (default)
#   make NO_DESKTOP_TAB=1     -> build WITHOUT it.
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
SRCS_COMMON = src/core/installer.c \
              src/core/partition_utils.c \
              src/core/utils.c \
              src/core/country_data.c \
              src/core/hard_steps.c \
              src/core/step_base_system.c \
              src/core/step_config_system.c \
              src/core/step_bootloader.c \
              src/core/flatpak-config.c \
              src/core/final_step.c \
              src/core/firts_filesystems.c \
              src/core/step_partition.c \
              src/core/step_format.c \
              src/i18n/lang_manager.c \
              src/i18n/lang_en.c \
              src/i18n/lang_es.c \
              src/i18n/lang_ja.c \
              src/i18n/loc.c

# Exclusive Void Linux module (xbps, repo keys, xbps-reconfigure...)
SRCS_VOID = src/core/installer_steps_void.c

# Desktop setup module (normal build only)
SRCS_DESKTOP = src/core/desktops-setup.c

# Rootfs-based base install (normal build only)
SRCS_ROOTFS = src/core/rootfs-base.c

# C++ wxWidgets UI
SRCS_UI = src/ui/wx_app.cpp \
          src/ui/wx_frame.cpp \
          src/ui/partition_dialog.cpp

ifneq ($(NO_DESKTOP_TAB),1)
SRCS = $(SRCS_COMMON) $(SRCS_VOID) $(SRCS_DESKTOP) $(SRCS_ROOTFS) $(SRCS_UI)
else
SRCS = $(SRCS_COMMON) $(SRCS_VOID) $(SRCS_UI)
endif
OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.c=.o)

# Build universal: objects in build/universal/ to avoid mixing flags
BUILDDIR_UNI = build/universal
SRCS_UNI = $(SRCS_COMMON) $(SRCS_UI)
OBJS_UNI = $(addprefix $(BUILDDIR_UNI)/,$(notdir $(SRCS_UNI:.c=.o)))
OBJS_UNI := $(OBJS_UNI:.cpp=.o)

# -----------------------------------------------------------------------
# Rules
# -----------------------------------------------------------------------

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
	$(CXX) $(OBJS) -o $(TARGET) $(LIBS)

# C source -> object (gcc)
%.o: %.c
	$(CC) $(CFLAGS) $(DESKTOP_FLAG) -c $< -o $@

# C++ source -> object (g++)
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(DESKTOP_FLAG) -c $< -o $@

# --- Universal build (without xbps / Void Linux) ---
$(BUILDDIR_UNI):
	mkdir -p $(BUILDDIR_UNI)

$(BUILDDIR_UNI)/%.o: src/core/%.c | $(BUILDDIR_UNI)
	$(CC) $(CFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

$(BUILDDIR_UNI)/%.o: src/i18n/%.c | $(BUILDDIR_UNI)
	$(CC) $(CFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

$(BUILDDIR_UNI)/%.o: src/ui/%.cpp | $(BUILDDIR_UNI)
	$(CXX) $(CXXFLAGS) -DUNIVERSAL_BUILD -c $< -o $@

universal: $(OBJS_UNI)
	$(CXX) $(OBJS_UNI) -o $(TARGET)_universal $(LIBS)
	@echo ""
	@echo ">>> Universal build ready: $(TARGET)_universal"
	@echo "    (No XBPS / Void Linux steps)"

# Clean
clean:
	rm -f $(TARGET) $(TARGET)_universal
	rm -f src/core/*.o src/core/*.d src/ui/*.o src/ui/*.d src/i18n/*.o src/i18n/*.d
	rm -f $(BUILD_STAMP)
	rm -rf $(BUILDDIR_UNI)

# Run (requires sudo to mount disks)
run: all
	sudo ./$(TARGET)

run-universal: universal
	sudo ./$(TARGET)_universal

-include $(OBJS:.o=.d)
