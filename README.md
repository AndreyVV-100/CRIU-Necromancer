# CRIU-Necromancer

## Intro

[CRIU](https://github.com/checkpoint-restore/criu) (stands for Checkpoint and Restore in Userspace) is a utility to checkpoint/restore Linux tasks. This project can dump and restore live processes, but can't do something with dead processes. The tool can try to resurrect dead process, if you have its coredump. It can be useful for debugging some programs.

CRIU-Necromancer is patching CRIU images by using data from ELF coredump, after that you can try restore dead process by criu.

## Usage

The tool is patching criu images using coredump file. This two things is required args for tool.

```bash
criu-necromancer -c <FILE> -i <PATH> [-h]
```

```
Options:
    -c <FILE>, --coredump <FILE>   # path to file with ELF coredump
    -i <PATH>, --images   <PATH>   # path to directory with donor's CRIU images
    -h, --help                     # get this help
```

## Help

### Standard things

#### Creating ELF coredump (dead process)

Now tool works only with full coredump. In order to tune enviroment, when such dumps are generating, I do this on Ubuntu 22.04:

```bash
echo 7 > /proc/self/coredump_filter
ulimit -c unlimited 
sudo service apport stop
```

After that you should start your program, but CRIU-Necromancer isn't working with programs that using tty. You should run program in new session without standart input/output streams.

```bash
setsid ./Backend.out < /dev/null &> /dev/null &
```

Backend.out will die and file ```core.pid``` (or other name) will be created.

#### Creating CRIU image (donor)

Start your program. It must wait something (e.g. signal after call ```pause()```). Data in this process isn't necessary equivalent to dead process.

```bash
setsid ./Backend.out < /dev/null &> /dev/null &
```

Find out donor's pid. You can use ```htop``` or this command:

```bash
ps -C Backend.out
```

Ok, now you can create donor's dump:

```bash
sudo criu dump -v4 -o dump.log -t PID && echo OK
```

#### Patching and restoring

It's time to use CRIU-Necromancer to patch CRIU images:

```bash
./criu-necromancer -c CORE -i PATH
```

After restore dead process must be stopped to not die. I use this command to restore:

```bash
sudo criu restore -v4 -o restore.log -d -s && echo OK
```

### Rseq syscall problem

Since glibc 2.35, rseq is called by default when a process starts. For this reason, criu fails restore after patching by necromancer. You should use env_without_rseq to fix it. Write

```bash
norseq PROG ARGS
```

to run PROG in the enviroment or don't write PROG and ARGS to run standart shell without rseq. In prior versions of glibc all working without norseq.

## Checklist

Some code in project is taken and inverted from [criu-coredump](https://github.com/checkpoint-restore/criu/blob/criu-dev/coredump/criu_coredump/coredump.py).

### Released

Necromancer working with:

1. core.img (core_entry in core.proto)

    - mtype - supported only x86-64 (but isn't checked) - ?
    - thread-info:
        - gpregs - in GoPrstatus - OK
        - fpregs - in GoFpregset - OK
    - tc:
        - Some in GoPrpsinfo - OK
        - Some in GoSiginfo  - OK
        - Some required fields isn't changing (exit_code, personality) - OK

2. files.img - skip in zero approximation. ToDo

3. ids.img - only renaming, but no working with it.

4. pstree.img - pstree_entry in pstree.proto:
    - all in GoPrpsinfo - OK (but no threads)

5. mm.img - mm_entry in mm.proto:
    - mm_saved_auxv - working in GoAuxv - OK
    - vmas - OK, but I hope thas phdrs <==> vmas (and pages too)
    - mm_arg, mm_env, mm_stack - Now is calculated by simple way. In other situations very hard, ToDo
    - mm_brk, mm_code, mm_data - simple writing it

6. pagemap.img - OK, but I hope thas phdrs <==> pages (and vmas too)

7. timens.img - responsible for time, skipping

8. tty-info.img - no working with it in coredump.py, skipping

### Plans

1. Add working with multi thread programs.

2. Add working with files.img, check filesizes, opened files.

3. Add calculating env, arg, stack.
