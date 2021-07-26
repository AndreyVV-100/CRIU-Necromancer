all: mode64

mode64: elf_parser.c fileworking.c
	gcc elf_parser.c fileworking.c -D MODE64 -Wall -Wextra

mode32: elf_parser.c fileworking.c
	gcc elf_parser.c fileworking.c -D MODE32 -Wall -Wextra
