#!/usr/bin/env bash
gcc src/core/main.c src/ui/ui.c src/ui/ui_callbacks.c src/ui/ui_partition.c src/core/partition_utils.c src/core/utils.c src/core/installer.c src/core/installer_steps.c -o neko_installer -Iinclude `pkg-config --cflags --libs gtk+-3.0` -lpthread
