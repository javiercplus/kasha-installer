# Kasha Installer

A modular Linux system installer built with C and GTK+ 3.0.

## Project Structure

- `include/`: Contains `neko_installer.h` with core data structures and function prototypes.
- `src/core/`: Application logic and installation steps.
    - `main.c`: Entry point and GTK initialization.
    - `installer.c`: Coordination of the installation thread and logging.
    - `installer_steps.c`: Individual steps (partitioning, formatting, copying files, secondary configuration).
    - `partition_utils.c`: Logic for disk discovery and partition mapping.
    - `utils.c`: Generic helpers (shell execution, UUID retrieval, EFI detection).
- `src/ui/`: GTK+ 3.0 user interface components.
    - `ui.c`: Main window development and tab structure.
    - `ui_partition.c`: Partition selection and manual partitioning dialogs.
    - `ui_callbacks.c`: Event handlers for navigation and user input.

## Features

- Automatic and Manual Partitioning:
    - UEFI Mode: GPT partition table, 512MB ESP (FAT32), and Ext4 root.
    - Legacy Mode: MBR (MS-DOS) partition table, single Ext4 root with bootable flag.
- Installation Modes: Clean install (erase disk), Install Alongside (dual-boot), or Manual.
- Encryption: LUKS support for partitions.
- Bootloader: GRUB installation for both EFI and BIOS systems.
- System Configuration: Locale, Hostname, Timezone, and User account management.

## Compilation and Dependencies

### Dependencies

Ensure you have the following installed:
- `gcc`
- `make`
- `pkg-config`
- `gtk+-3.0` development headers
- For use your system require LUKS ,bash, Grub Install, xxd and sed
-  If you use Devuan or Arch, use the universal branch.
# if you wanna change the logo in the installer 

first convert your image logo wit xxd
```
xxd -i logo.png > include/logo.h
```
and replace logo.h in the project<3
### Build Instructions

To compile the installer:

```bash
make
```

To clean the build artifacts:

```bash
make clean
```

## Running the Installer

The installer requires root privileges to manipulate disks, format partitions, and mount filesystems.

```bash
sudo ./neko_installer
```

Alternatively, use the convenience rule:

```bash
make run
```
