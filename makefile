CC := gcc
CFLAGS := -Wall -Wextra
FILES  := criu_necromancer.c fileworking.c
LDLIBS := -lprotobuf-c

OBJ_DESCRIPTOR := Images/google/protobuf/descriptor.o

OBJS := Images/opts.o Images/rlimit.o Images/timer.o Images/creds.o Images/sa.o  \
Images/siginfo.o Images/vma.o Images/mm.o Images/pagemap.o Images/pstree.o \
Images/core-aarch64.o Images/core-arm.o \
Images/core-mips.o Images/core-ppc64.o Images/core-s390.o Images/core-x86.o Images/core.o

.PHONY: all mode64 mode32 clean

all: mode64

$(OBJ_DESCRIPTOR): Images/google/protobuf/descriptor.proto
	protoc-c $^ --c_out .
	$(CC) -c Images/google/protobuf/descriptor.pb-c.c -o $@ -I .

Images/%.o: Images/%.pb-c.c Images/%.pb-c.h
	$(CC) -c $< -o $@ -I .

Images/%.pb-c.c Images/%.pb-c.h: Images/%.proto
	protoc-c $^ --c_out Images --proto_path Images

mode64: $(FILES) $(OBJ_DESCRIPTOR) $(OBJS)
	$(CC) $^ -D MODE64 $(CFLAGS) $(LDLIBS) -o criu-necromancer

clean:
	rm -f Images/*.pb-c.c
	rm -f Images/*.pb-c.h
	rm -f Images/*.o
	rm -f Images/google/protobuf/descriptor.pb-c.c
	rm -f Images/google/protobuf/descriptor.pb-c.h
	rm -f Images/google/protobuf/descriptor.o	

# ToDo: mode32: criu_necromancer fileworking.c
#	gcc criu_necromancer fileworking.c -D MODE32 -Wall -Wextra
