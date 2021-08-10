all: mode64

# ToDo: rewrite make

OBJS := Images/core.o  Images/core-aarch64.o Images/core-arm.o Images/core-mips.o \
Images/core-ppc64.o Images/core-s390.o Images/core-x86.o Images/creds.o Images/mm.o \
Images/opts.o Images/pagemap.o Images/pstree.o Images/rlimit.o Images/sa.o \
Images/siginfo.o Images/timer.o Images/vma.o Images/google/protobuf/descriptor.o

FILES := criu_necromancer.c fileworking.c

LDLIBS := -lprotobuf-c

mode64: $(FILES) $(OBJS)
	gcc $^ -D MODE64 -Wall -Wextra $(LDLIBS)

mode32: criu_necromancer fileworking.c
	gcc criu_necromancer fileworking.c -D MODE32 -Wall -Wextra
