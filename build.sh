#!/usr/bin/env bash
gcc main.c ui.c utils.c installer.c -o neko_installer `pkg-config --cflags --libs gtk+-3.0`
