CC := gcc
CFLAGS := -Wall -Wextra
FILES  := criu_necromancer.c fileworking.c
LDLIBS := -lprotobuf-c

OBJ_DESCRIPTOR := Images/google/protobuf/descriptor.o

OBJS := Images/opts.o
OBJS := $(OBJS) Images/rlimit.o
OBJS := $(OBJS) Images/timer.o
OBJS := $(OBJS) Images/creds.o
OBJS := $(OBJS) Images/sa.o
OBJS := $(OBJS) Images/siginfo.o
OBJS := $(OBJS) Images/vma.o
OBJS := $(OBJS) Images/mm.o
OBJS := $(OBJS) Images/pagemap.o
OBJS := $(OBJS) Images/pstree.o
OBJS := $(OBJS) Images/core-aarch64.o
OBJS := $(OBJS) Images/core-arm.o
OBJS := $(OBJS) Images/core-mips.o
OBJS := $(OBJS) Images/core-ppc64.o
OBJS := $(OBJS) Images/core-s390.o
OBJS := $(OBJS) Images/core-x86.o
OBJS := $(OBJS) Images/core.o
OBJS := $(OBJS) Images/fown.o
OBJS := $(OBJS) Images/regfile.o
OBJS := $(OBJS) Images/sk-opts.o
OBJS := $(OBJS) Images/sk-inet.o
OBJS := $(OBJS) Images/ns.o
OBJS := $(OBJS) Images/packet-sock.o
OBJS := $(OBJS) Images/sk-netlink.o
OBJS := $(OBJS) Images/eventfd.o
OBJS := $(OBJS) Images/eventpoll.o
OBJS := $(OBJS) Images/signalfd.o
OBJS := $(OBJS) Images/tun.o
OBJS := $(OBJS) Images/timerfd.o
OBJS := $(OBJS) Images/fh.o
OBJS := $(OBJS) Images/fsnotify.o
OBJS := $(OBJS) Images/ext-file.o
OBJS := $(OBJS) Images/sk-unix.o
OBJS := $(OBJS) Images/fifo.o
OBJS := $(OBJS) Images/pipe.o
OBJS := $(OBJS) Images/tty.o
OBJS := $(OBJS) Images/memfd.o
OBJS := $(OBJS) Images/bpfmap-file.o
OBJS := $(OBJS) Images/fdinfo.o 

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

env_without_rseq: env_without_rseq.c
	$(CC) $^ $(CFLAGS) -o $@

clean:
	rm -f Images/*.pb-c.c
	rm -f Images/*.pb-c.h
	rm -f Images/*.o
	rm -f Images/google/protobuf/descriptor.pb-c.c
	rm -f Images/google/protobuf/descriptor.pb-c.h
	rm -f Images/google/protobuf/descriptor.o
	rm -f criu-necromancer

# ToDo: mode32: criu_necromancer fileworking.c
#	gcc criu_necromancer fileworking.c -D MODE32 -Wall -Wextra
