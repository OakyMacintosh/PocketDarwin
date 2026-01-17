CC := clang
CXX := clang++
CFLAGS := -ffreestanding -O2 -Wall -Wextra -Wimplicit-function-declaration
AS := nasn
ASFLAGS := # later
sources := loader.c \
	BootMenu.c \
	Framebuffer.c \
	ACPIParser.c \
	Platform/plist/plist.c \
	Platform/OpenPartitionDxe/Gpt.c \
	Platform/OpenPartitionDxe/Mbr.c \
	Platform/Kextld.c



all:
	$(CC) $(CFLAGS) $(sources) -o %.o
