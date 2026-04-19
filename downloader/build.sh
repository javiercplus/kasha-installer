#!/bin/bash
gcc main.c -o downloader $(pkg-config --cflags --libs gtk+-3.0) -lpthread