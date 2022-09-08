#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/procfs.h>
#include <signal.h>
#include <compel/asm/fpu.h>
#include <sys/mman.h>
#include <getopt.h>
// #include <sys/user.h> included in procfs.h
#include "criu_necromancer.h"
#include "fileworking.h"

int main (int argc, char** argv)
{
    ArgInfo args = {};
    if (ParseArguments (argc, argv, &args))
        return 0;

    Elf* elf = ElfConstructor (args.elf);
    Images* imgs = ImagesConstructor (&args);

    if (elf != NULL && imgs != NULL)
    {
        GoPhdrs (elf, imgs);
        ImagesWrite (imgs, &args);
    }

    ArgInfoFree (&args);
    ElfDestructor (elf);
    ImagesDestructor (imgs);
    return 0;
}

int ParseArguments (int argc, char** argv, ArgInfo* args) // ToDo: getopt
{
    assert (argc);
    assert (argv);
    assert (args);

    int opt_found = 0;
    struct option longopts[] = {{"coredump", 1, NULL, 'c'},
                                {"images",   1, NULL, 'i'},
                                {"help",     0, NULL, 'h'},
                                {NULL,       0, NULL,   0}};

    while ((opt_found = getopt_long (argc, argv, "c:i:h", longopts, NULL)) != -1)
    {
        switch (opt_found)
        {
            case 'c':
                args->elf = optarg;
                break;
            
            case 'i':
                args->criu_dump_path = optarg;
                break;

            case 'h':
            case '?':
            default:
                printf (                                         "\n"
                    "Usage:"                                     "\n"
                    "  criu-necromancer -c PATH -p PATH -i PID"  "\n"
                                                                 "\n"
                    "Options:"                                   "\n"
                    "  -c  [--coredump]   path to coredump file" "\n"
                    "  -i  [--images  ]   path to criu images"   "\n"
                                                                 "\n");
                return -1;
        }
    }

    #define check_pointer(field, str)   if (args->field == NULL)                               \
                                        {                                                      \
                                            fprintf (stderr, "Error: no " str " in input.\n"); \
                                            *args = EMPTY_ARGINFO;                             \
                                            return 1;                                          \
                                        }

    check_pointer (criu_dump_path, "criu dump path");
    check_pointer (elf, "coredump path");
    #undef check_pointer

    return 0;
}

// C-style overloading

char* CreateImagePath  (const char* path, const char* name)
{
    char buffer[MAX_PATH_LEN] = "";
    snprintf (buffer, MAX_PATH_LEN - 1, "%s/%s.img", path, name);
    char* ret = strndup (buffer, MAX_PATH_LEN);

    if (!ret)
        fprintf (stderr, "Error: Can't allocate memory.\n");
    return ret;
}

char* CreateImagePathWithPid (const char* path, const char* name, int pid)
{
    char buffer[MAX_PATH_LEN] = "";
    snprintf (buffer, MAX_PATH_LEN, "%s/%s-%d.img", path, name, pid);
    char* ret = strndup (buffer, MAX_PATH_LEN);
    
    if (!ret)
        fprintf (stderr, "Error: Can't allocate memory.\n");
    return ret;
}

void ArgInfoFree (ArgInfo* args)
{
    // ToDo: Now is simple. Maybe delete it?
    assert (args);

    *args = EMPTY_ARGINFO;
    return;
}

Images* ImagesConstructor (ArgInfo* args)
{
    assert (args);

    Images* imgs = (Images*) calloc (1, sizeof (*imgs)); 
    if (imgs == NULL)                                   
    {                                                     
        perror ("Can't create images");
        ArgInfoFree (args);                        
        return NULL;                                      
    }

    #define check_retval(retval) if ((retval))                                             \
                                    {                                                      \
                                        fprintf (stderr, "Error: Can't create images.\n"); \
                                        ImagesDestructor (imgs);                           \
                                        return NULL;                                       \
                                    }

    check_retval (ReadOnlyOneMessage (args->criu_dump_path, "pstree", 0,
                      (MessageUnpacker*) pstree_entry__unpack, (ProtobufCMessage**) &(imgs->pstree), MY_PSTREE_MAGIC) == -1);
    args->criu_dump_id = imgs->pstree->pid;

    check_retval (ReadOnlyOneMessage (args->criu_dump_path, "core", args->criu_dump_id,
                      (MessageUnpacker*) core_entry__unpack,   (ProtobufCMessage**) &(imgs->core),   MY_CORE_MAGIC)   == -1);
    check_retval (ReadOnlyOneMessage (args->criu_dump_path, "mm",   args->criu_dump_id, 
                      (MessageUnpacker*) mm_entry__unpack,     (ProtobufCMessage**) &(imgs->mm),     MY_MM_MAGIC)     == -1);
    
    // Start pagemap:
    char* pagemap_filename = CreateImagePathWithPid (args->criu_dump_path, "pagemap", args->criu_dump_id);
    check_retval (pagemap_filename == NULL)
    check_retval ((imgs->pagemap = StartImageWriting (pagemap_filename, MY_PAGEMAP_MAGIC)) == NULL)
    free (pagemap_filename);
    
    // In my situation criu always creates pagemap-pid.img and pages-1.img. I don't know, how could it be otherwise.
    PagemapHead head = PAGEMAP_HEAD__INIT;
    head.pages_id = 1;
    WriteMessage ((MessagePacker*) pagemap_head__pack, (ProtobufCMessage*) &head, pagemap_head__get_packed_size (&head), imgs->pagemap);

    imgs->pages = fopen ("pages-1.img", "w"); // It's raw data, there is no protobuf messages.
    check_retval (imgs->pages == NULL)

    #undef check_retval
    return imgs;
}

void ChangeImagePid (const char* path, const char* name, int old_pid, int new_pid)
{
    assert (path);
    assert (name);
    assert (old_pid);
    assert (new_pid);

    char* old_name = CreateImagePathWithPid (path, name, old_pid);
    char* new_name = CreateImagePathWithPid (path, name, new_pid);
    rename (old_name, new_name);
    free (old_name);
    free (new_name);
    return;
}

void ImagesWrite (Images* imgs, ArgInfo* args)
{
    assert (imgs);
    assert (args);
    assert (args->criu_dump_path);
    assert (args->criu_dump_id);
    assert (imgs->pstree);
    assert (imgs->core);
    assert (imgs->mm);

    WriteOnlyOneMessage (args->criu_dump_path, "pstree",               0, (MessagePacker*) pstree_entry__pack, imgs->pstree, 
                         pstree_entry__get_packed_size (imgs->pstree), MY_PSTREE_MAGIC);

    WriteOnlyOneMessage (args->criu_dump_path, "core", imgs->pstree->pid, (MessagePacker*) core_entry__pack,   imgs->core,
                         core_entry__get_packed_size   (imgs->core),   MY_CORE_MAGIC);

    WriteOnlyOneMessage (args->criu_dump_path, "mm",   imgs->pstree->pid, (MessagePacker*) mm_entry__pack,     imgs->mm, 
                         mm_entry__get_packed_size     (imgs->mm),     MY_MM_MAGIC);

    // In my images I found 2 files, that need to be renamed: fs and ids.
    // core, mm and pagemap are renamed yet.
    // ToDo: find all files in documentation.
    fclose (imgs->pagemap);
    imgs->pagemap = NULL; // ToDo: OK???

    ChangeImagePid (args->criu_dump_path, "fs",      args->criu_dump_id, imgs->pstree->pid);
    ChangeImagePid (args->criu_dump_path, "ids",     args->criu_dump_id, imgs->pstree->pid);
    ChangeImagePid (args->criu_dump_path, "pagemap", args->criu_dump_id, imgs->pstree->pid);

    return;
}

void ImagesDestructor (Images* imgs)
{
    if (!imgs)
        return;

    pstree_entry__free_unpacked  (imgs->pstree,  NULL);
    core_entry__free_unpacked    (imgs->core,    NULL);
    mm_entry__free_unpacked      (imgs->mm,      NULL);

    if (imgs->pagemap) fclose (imgs->pagemap);
    if (imgs->pages)   fclose (imgs->pages);

    *imgs = EMPTY_IMAGES;
    free (imgs);
    return;
}

Elf* ElfConstructor (const char* filename)
{
    assert (filename);
    Elf* elf_result = (Elf*) calloc (1, sizeof (*elf_result)); // calloc <==> all pointers are NULL 

    #define check_pointer(name) if (elf_result->name == NULL)   \
                                {                               \
                                    ElfDestructor (elf_result); \
                                    return NULL;                \
                                }

    elf_result->buf = ReadFile (filename);
    check_pointer (buf);    

    elf_result->elf_hdr = CheckElfHdr (elf_result->buf);
    check_pointer (elf_hdr);

    elf_result->phdr_table = CheckPhdrs (elf_result);
    check_pointer (phdr_table);

    #undef check_pointer
    return elf_result;
}

void ElfDestructor (Elf* elf)
{
    if (elf)
    {
        free (elf->buf);
        free (elf);
    }

    return;
}

Elf_Ehdr* CheckElfHdr (const char* buf)
{
    assert (buf);
    Elf_Ehdr* elf_hdr = (Elf_Ehdr*) buf;

    #define check(condition, error_type) if (!(condition))                                 \
                                         {                                                 \
                                             fprintf (stderr, "Error: " error_type ".\n"); \
                                             return NULL;                                  \
                                         }

    // ToDo: or check (strncmp (elf_hdr->e_ident, ELFMAG) == 0)?
    check (elf_hdr->e_ident[0] == ELFMAG0, "Bad magic number byte 0");
    check (elf_hdr->e_ident[1] == ELFMAG1, "Bad magic number byte 1");
    check (elf_hdr->e_ident[2] == ELFMAG2, "Bad magic number byte 2");
    check (elf_hdr->e_ident[3] == ELFMAG3, "Bad magic number byte 3");
    check (elf_hdr->e_ident[4] == ELFCLASS32 || elf_hdr->e_ident[4] == ELFCLASS64, "Bad object class");
    check (elf_hdr->e_ident[6] == EV_CURRENT, "Bad file version in e_ident")

    check (elf_hdr->e_type == ET_CORE, "This program need core elf file"); // WARNING: only for my task
    check (elf_hdr->e_machine != EM_NONE, "Bad machine");
    check (elf_hdr->e_version == EV_CURRENT, "Bad file version in e_version");

    #undef check
    return elf_hdr;
}

Elf_Phdr* CheckPhdrs (Elf* elf)
{
    assert (elf);
    assert (elf->buf);
    assert (elf->elf_hdr);

    elf->phnum = elf->elf_hdr->e_phnum;
    elf->phdr_table = (Elf_Phdr*) (elf->buf + elf->elf_hdr->e_phoff);

    for (Elf_Half i_phdr; i_phdr < elf->phnum; i_phdr++)
    {
        if (elf->phdr_table[i_phdr].p_type == PT_LOAD  && elf->phdr_table[i_phdr].p_filesz > elf->phdr_table[i_phdr].p_memsz)
        {
            fprintf (stderr, "Error: Bad program header number %d:\n"
                             "p_memsz  = 0x%lX\n" 
                             "p_filesz = 0x%lX\n"
                             "p_memsz < p_filesz\n", 
                             i_phdr, elf->phdr_table[i_phdr].p_memsz, elf->phdr_table[i_phdr].p_filesz);
            elf->phdr_table = NULL;
            elf->phnum = 0;
            break;
        }
    }

    return elf->phdr_table;
}

void GoPhdrs (Elf* elf, Images* imgs)
{
    assert (elf);
    size_t vma_counter = 0;

    for (size_t i_phdr = 0; i_phdr < elf->phnum; i_phdr++)
    {
        switch (elf->phdr_table[i_phdr].p_type)
        {
            case PT_NOTE:
                GoNhdrs (elf->buf + elf->phdr_table[i_phdr].p_offset, elf->phdr_table[i_phdr].p_filesz, imgs);
                break;

            case PT_LOAD:
                if (GoLoadPhdr (elf, elf->phdr_table + i_phdr, imgs, vma_counter))
                {
                    return; // ToDo: Ban writing?
                }
                vma_counter++;
                break;

            case PT_NULL:
                break; // ?

            default:
                fprintf (stderr, "Warning: I don't know, how to parse program header No %lu.\n", i_phdr);
                break;
        }
    }
}

void GoNhdrs (void* nhdrs, Elf_Xword p_filesz, Images* imgs)
{
    assert (nhdrs);
    assert (imgs);
    size_t align = 0, common_align = 0;

    while (common_align < p_filesz)
    {
        Elf_Nhdr* nhdr_now = (Elf_Nhdr*)  nhdrs;

        switch (nhdr_now->n_type)
        {
            // ToDo: auto align and return err status

            case NT_PRPSINFO:
                align = GoPrpsinfo (nhdr_now, imgs);
                break;
            
            case NT_PRSTATUS:
                align = GoPrstatus (nhdr_now, imgs);
                break;

            case NT_FPREGSET:
                align = GoFpregset (nhdr_now, imgs);
                break;

            case NT_X86_XSTATE:
                align = GoX86_State (nhdr_now, imgs);
                break;

            case NT_SIGINFO:
                align = GoSiginfo (nhdr_now, imgs);
                break;

            case NT_AUXV:
                align = GoAuxv (nhdr_now, imgs);
                break;

            case NT_FILE:
                align = GoFile (nhdr_now, imgs);
                break;

            default:
                // ToDo: Give more debug info
                fprintf (stderr, "Warning: I don't know, how to parse note header with type %u.\n"
                                 "Is it gdb note header?\n", 
                        (nhdr_now)->n_type);
                align = sizeof (*nhdr_now) + GetAlignedSimple (nhdr_now->n_descsz) + GetAlignedSimple (nhdr_now->n_namesz);
                break;
        }

        if (!align)
            return;
        nhdrs += align;
        common_align += align;
    }
    
    return;
}

/*
    For note functions:
    Note = {Nhdr, name, desc}

    Name and desc have align = 4 or 8
*/

// ToDo: Check name?
// Note for offset: align of name = 4 or 8
#define NHDR_START(type, name, is_array)  \
            assert (nhdr);                                                                                     \
            assert (imgs);                                                                                     \
            assert (nhdr->n_type == type);                                                                     \
                                                                                                               \
            if (nhdr->n_descsz != sizeof (name##_t) && !is_array)                                              \
                fprintf (stderr, "Error: Bad n_descsz in %s:\n"                                                \
                                 "Expected: sizeof (" #name ") = %lu\n"                                        \
                                 "Detected: %u\n", __func__, sizeof (name##_t), nhdr->n_descsz);               \
                                                                                                               \
            size_t offset = sizeof (*nhdr) + GetAlignedSimple (nhdr->n_namesz);                                \
            name##_t* name = (name##_t*) (((void*) nhdr) + offset)

// align of desc = 4
#define NHDR_RETURN return offset + GetAlignedSimple (nhdr->n_descsz)

size_t GoPrpsinfo (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_PRPSINFO, prpsinfo, 0);

    #define TASK_ALIVE 0x1
    #define TASK_DEAD  0x2
    #define TASK_STOPPED 0x3

    /* switch (prpsinfo->pr_state)
    {
        case 0:
            imgs->core->tc->task_state = TASK_ALIVE;
            break;
        case 4:
            imgs->core->tc->task_state = TASK_DEAD;
            break;
        case 3:
            imgs->core->tc->task_state = TASK_STOPPED;
            break;
        default:
            fprintf (stderr, "Warning: I don't know, what I must do with prpsinfo->pr_state = 0x%X\n",
                             (uint32_t) (prpsinfo->pr_state));
            break;
    }
 */
    imgs->core->thread_core->sched_prio = prpsinfo->pr_nice; // sched_prio not in thread_core?
    imgs->core->tc->flags = prpsinfo->pr_flag;
    imgs->core->thread_core->creds->uid = prpsinfo->pr_uid;
    imgs->core->thread_core->creds->gid = prpsinfo->pr_gid;
    imgs->pstree->pid  = prpsinfo->pr_pid;
    imgs->pstree->ppid = 0; // error if prpsinfo->pr_ppid, criu in dumping writes 0 here.
    imgs->pstree->pgid = prpsinfo->pr_pgrp;
    imgs->pstree->sid  = prpsinfo->pr_sid;

    // ToDo: This code was created only for one-thread program
    if (imgs->pstree->n_threads == 1)
        imgs->pstree->threads[0] = prpsinfo->pr_pid;
    else
    {
        fprintf (stderr, "Warning: number of threads in programs isn't equal.\n");
        imgs->pstree->n_threads = 1; // I think that this action didn't affect to free()
        imgs->pstree->threads[0] = prpsinfo->pr_pid; // but if threads == NULL?
    }

    // prpsinfo->pr_psargs; // psarg - comand line's arguments. 
    // They are contained in stack, working with psarg, as I think, useless.

    // It's useless (because exec_names are equal) and dangerous (because pr_fname[16])
    // free (imgs->core->tc->comm); 
    // imgs->core->tc->comm = strdup (prpsinfo->pr_fname);

    #undef TASK_ALIVE
    #undef TASK_DEAD
    #undef TASK_STOPPED
    NHDR_RETURN;
}

size_t GoPrstatus (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_PRSTATUS, prstatus, 0);

    // ToDo: Other platforms?
    UserX86RegsEntry* regs = imgs->core->thread_info->gpregs;

    // pr_reg <==> user_regs_struct, but haven't this type
    regs->r15 = prstatus->pr_reg[0];
    regs->r14 = prstatus->pr_reg[1];
    regs->r13 = prstatus->pr_reg[2];
    regs->r12 = prstatus->pr_reg[3];
    regs->bp  = prstatus->pr_reg[4];
    regs->bx  = prstatus->pr_reg[5];
    regs->r11 = prstatus->pr_reg[6];
    regs->r10 = prstatus->pr_reg[7];
    regs->r9  = prstatus->pr_reg[8];
    regs->r8  = prstatus->pr_reg[9];
    regs->ax  = prstatus->pr_reg[10];
    regs->cx  = prstatus->pr_reg[11];
    regs->dx  = prstatus->pr_reg[12];
    regs->si  = prstatus->pr_reg[13];
    regs->di  = prstatus->pr_reg[14];
    regs->orig_ax = prstatus->pr_reg[15];
    regs->ip  = prstatus->pr_reg[16];
    regs->cs  = prstatus->pr_reg[17];
    regs->flags = prstatus->pr_reg[18];
    regs->sp  = prstatus->pr_reg[19];
    regs->ss  = prstatus->pr_reg[20];
    regs->fs_base = prstatus->pr_reg[21];
    regs->gs_base = prstatus->pr_reg[22];
    regs->ds  = prstatus->pr_reg[23];
    regs->es  = prstatus->pr_reg[24];
    regs->fs  = prstatus->pr_reg[25];
    regs->gs  = prstatus->pr_reg[26];

    NHDR_RETURN;    
}

size_t GoFpregset (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_FPREGSET, elf_fpregset, 0);

    UserX86FpregsEntry* regs = imgs->core->thread_info->fpregs;
    regs->cwd = elf_fpregset->cwd;
    regs->swd = elf_fpregset->swd;
    regs->twd = elf_fpregset->ftw;
    regs->fop = elf_fpregset->fop;
    regs->rip = elf_fpregset->rip;
    regs->rdp = elf_fpregset->rdp;
    regs->mxcsr = elf_fpregset->mxcsr;
    regs->mxcsr_mask = elf_fpregset->mxcr_mask;
    
    if (sizeof (elf_fpregset->st_space) / sizeof (elf_fpregset->st_space[0]) != regs->n_st_space)
        fprintf (stderr, "Warning: st_space in coredump isn't equal st_space in criu images.\n"
                         "n_st_space [criu] = %zu\n"
                         "sizeof (st_space) [coredump] = %zu\n", regs->n_st_space, sizeof (elf_fpregset->st_space));
    memcpy (regs->st_space,  elf_fpregset->st_space,  regs->n_st_space);

    if (sizeof (elf_fpregset->xmm_space) / sizeof (elf_fpregset->xmm_space[0]) != regs->n_xmm_space)
        fprintf (stderr, "Warning: xmm_space in coredump isn't equal xmm_space in criu images.\n"
                         "n_xmm_space [criu] = %zu\n"
                         "sizeof (xmm_space) [coredump] = %zu\n", regs->n_xmm_space, sizeof (elf_fpregset->xmm_space));
    memcpy (regs->xmm_space, elf_fpregset->xmm_space, sizeof (elf_fpregset->xmm_space));
    
    NHDR_RETURN;
}

size_t GoX86_State (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_X86_XSTATE, xsave, 0);
    UserX86FpregsEntry* regs = imgs->core->thread_info->fpregs;

    if (regs->xsave == NULL)
    {
        fprintf (stderr, "Warning: UserX86XsaveEntry doesn't exist yet. Creating it...\n");
        regs->xsave = (UserX86XsaveEntry*) calloc (1, sizeof (*(regs->xsave)));
        user_x86_xsave_entry__init (regs->xsave);
    }

    // regs->xsave->xstate_bv = xsave->xsave_hdr.xstate_bv;
    free (regs->xsave->ymmh_space);
    regs->xsave->ymmh_space = (uint32_t*) calloc (1, sizeof (xsave->ymmh));
    memcpy (regs->xsave->ymmh_space, &(xsave->ymmh), sizeof (xsave->ymmh));
    regs->xsave->n_ymmh_space = sizeof (xsave->ymmh) / sizeof (regs->xsave->ymmh_space[0]);

    // ToDo: Do I need do something another here?

    NHDR_RETURN;
}

size_t GoSiginfo (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_SIGINFO, siginfo, 0);
    NHDR_RETURN;
    static size_t new_pos = 0;
    SignalQueueEntry* signals_s = imgs->core->tc->signals_s;

    // ToDo: new_pos < n_signals after working program?
    if (new_pos >= signals_s->n_signals)
    {
        // fprintf (stderr, "Error: amount of siginfo_t structs >= n_signals. Maybe fix it in this program?\n");
        // NHDR_RETURN;

        // ToDo: I'm not sure, but it can work
        signals_s->n_signals++; // always imgs->core->tc->signals_s->n_signals + 1 > new_pos
        signals_s->signals = realloc (signals_s->signals, signals_s->n_signals * sizeof (*signals_s->signals)); // realloc (NULL) = malloc
        signals_s->signals[new_pos] = calloc (1, sizeof (*signals_s->signals[new_pos]));
        siginfo_entry__init (signals_s->signals[new_pos]);
    }
    
    signals_s->signals[new_pos]->siginfo.len = sizeof (*siginfo);
    free (signals_s->signals[new_pos]->siginfo.data);
    signals_s->signals[new_pos]->siginfo.data = calloc (1, sizeof (*siginfo));
    memcpy (signals_s->signals[new_pos]->siginfo.data, siginfo, sizeof (*siginfo));

    /*
    I found it in criu/cr-restore.c
        static int signal_to_mem(SiginfoEntry *se)
        {
            siginfo_t *info, *t;

            info = (siginfo_t *)se->siginfo.data;
            t = rst_mem_alloc(sizeof(siginfo_t), RM_PRIVATE);
            if (!t)
                return -1;

            memcpy(t, info, sizeof(*info));

            return 0;
        }
    */

    new_pos++;
    NHDR_RETURN;
}

size_t GoAuxv (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_AUXV, Elf_auxv, 1);

    if (nhdr->n_descsz % (2 * sizeof (*imgs->mm->mm_saved_auxv))) // ToDo: It's ok?
        fprintf (stderr, "Warning: In my opinion, auxv in coredump is broken.\n");

    free (imgs->mm->mm_saved_auxv);
    imgs->mm->mm_saved_auxv = (uint64_t*) calloc (1, nhdr->n_descsz);
    memcpy (imgs->mm->mm_saved_auxv, Elf_auxv, nhdr->n_descsz);
    imgs->mm->n_mm_saved_auxv = nhdr->n_descsz / sizeof (*imgs->mm->mm_saved_auxv);

    NHDR_RETURN;
}

size_t GoFile (Elf_Nhdr* nhdr, Images* imgs) // ToDo: not working function
{
    NHDR_START (NT_FILE, file, 1);
    (void) file;
    VmaEntry** vmas = imgs->mm->vmas;

    for (size_t i_vma = 0, i_file = 0; i_vma < imgs->mm->n_vmas; i_vma++)
    {
        if (vmas[i_vma]->shmid == 0) // isn't file
            continue;

        // This printf shows full match between NT_FILE nhdr and vma with shmid != 0.
        // But now I don't know how to use it because PT_LOAD phdrs contains all necessary information.
        // printf ("shmid = %ld filename = %s [coredump] = %lX [criu] = %lX\n", 
        //                                 vmas[i_vma]->shmid,
        //                                 GetFilenameByNumInNTFile (file, i_file),
        //                                 file->array[i_file].end - file->array[i_file].start,
        //                                 vmas[i_vma]->end - vmas[i_vma]->start);

        i_file++;
    }

    /*  ToDo: this is incorrect function. Rewrite it.
    if (imgs->mm->n_vmas != (size_t) file->count)
    {
        fprintf (stderr, "Error (FIXME): n_vmas != count in NT_FILE.\n");
        return 0;
    }

    // ToDo: names? See coredump.py:583
    // ToDo: Maybe do this more carefully?
    for (int i_vma = 0; i_vma < file->count; i_vma++)
    {
        imgs->mm->vmas[i_vma]->start = file->array[i_vma].start;
        imgs->mm->vmas[i_vma]->end   = file->array[i_vma].end;
        imgs->mm->vmas[i_vma]->pgoff = file->array[i_vma].file_ofs * PAGESIZE;     
    }
    */

    NHDR_RETURN;
}

char* GetFilenameByNumInNTFile (file_t* file, size_t file_num) // ToDo: Very slow, if you use it in cycle
{
    assert (file);

    char* str = (char*) file + sizeof (*file) + file->count * sizeof (file->array[0]);
    for (size_t i_str = 0; i_str < file_num; i_str++)
        str = str + strlen (str) +  1;

    return str;
}

#undef NHDR_START
#undef NHDR_RETURN

int GoLoadPhdr (Elf* elf, Elf_Phdr* phdr, Images* imgs, size_t vma_counter) 
{
    assert (elf);
    assert (phdr);
    assert (imgs);

#define check_correct(cond, ...) if ((cond))                        \
                                 {                                  \
                                     fprintf (stderr, __VA_ARGS__); \
                                     return -1;                     \
                                 }


    check_correct (vma_counter >= imgs->mm->n_vmas, "Error: criu dump has less vmas than needed.\n")
    check_correct (phdr->p_filesz % PAGESIZE, "Error: PT_LOAD phdr size isn't aligned.\n")
    check_correct (phdr->p_memsz > phdr->p_filesz, "Error: In some program header memsz > filesz."
                                                   "It means, that coredump isn't full and generated images is incorrect.\n")

    VmaEntry* vma = imgs->mm->vmas[vma_counter];
    check_correct (phdr->p_filesz != vma->end - vma->start, "Error: vma and following phdr have different sizes. vma_counter = %zu\n", vma_counter)

    MmChangeIfNeeded (imgs->mm, vma, phdr);

    vma->start = phdr->p_vaddr;
    vma->end   = phdr->p_vaddr + phdr->p_filesz;
    // pgoff, shmid, prot, flags, status, flags, fd, fdflags --- I hope it's equal

    // ToDo: criu writes: "Trying to restore page for non-private VMA", but this vma has flags: VMA_AREA_VSYSCALL | VMA_ANON_PRIVATE
    // Maybe check !(vma->flags && VMA_AREA_REGULAR) ???
    if (vma->status & VMA_AREA_VSYSCALL)
        return 0;

    PagemapEntry pagemap = PAGEMAP_ENTRY__INIT;
    pagemap.vaddr = phdr->p_vaddr;
    pagemap.nr_pages = phdr->p_filesz / PAGESIZE;
    pagemap.has_flags = 1; // ToDo: Is it correct?
    pagemap.flags = PE_PRESENT;

    WriteMessage ((MessagePacker*) pagemap_entry__pack, (ProtobufCMessage*) &pagemap, 
                  pagemap_entry__get_packed_size (&pagemap), imgs->pagemap);

    fwrite ((char*) elf->buf + phdr->p_offset, 1, phdr->p_filesz, imgs->pages);
    #undef check_correct
    return 0;
}

void MmChangeIfNeeded (MmEntry* mm, VmaEntry* vma, Elf_Phdr* phdr)
{
    assert (mm);
    assert (vma);
    assert (phdr);

    if (mm->mm_start_code == vma->start)
    {
        mm->mm_start_code = phdr->p_vaddr;
        mm->mm_end_code   = phdr->p_vaddr + phdr->p_filesz;
        return;
    }

    if (mm->mm_start_data == vma->start)
    {
        mm->mm_start_data = phdr->p_vaddr;
        mm->mm_end_data   = phdr->p_vaddr + phdr->p_filesz;
        return;
    }

    if (vma->flags & MAP_GROWSDOWN)
    {
        mm->mm_start_stack = phdr->p_vaddr + mm->mm_start_stack - vma->start;
        mm->mm_arg_start   = phdr->p_vaddr + mm->mm_arg_start   - vma->start;
        mm->mm_arg_end     = phdr->p_vaddr + mm->mm_arg_end     - vma->start;
        mm->mm_env_start   = phdr->p_vaddr + mm->mm_env_start   - vma->start;
        mm->mm_env_end     = phdr->p_vaddr + mm->mm_env_end     - vma->start;
        return;
    }

    if (vma->status & VMA_AREA_HEAP)
    {
        mm->mm_start_brk = phdr->p_vaddr;
        mm->mm_brk       = phdr->p_vaddr + phdr->p_filesz;
        return;
    }

    return;
}

uint32_t GetVmaProtByPhdr (Elf_Word phdr_flags)
{
    // Given from criu-coredump
    #define PROT_READ  0x1
    #define PROT_WRITE 0x2
    #define PROT_EXEC  0x4
    uint32_t flags = 0;

    if (phdr_flags & PF_R)
        flags |= PROT_READ;

    if (phdr_flags & PF_W)
        flags |= PROT_WRITE;

    if (phdr_flags & PF_X)
        flags |= PROT_EXEC;

    return flags;
}

FILE* StartImageReading (const char* filename, CriuMagic expected_magic)
{
    assert (filename);

    FILE* file = fopen (filename, "r");
    if (!file)
    {
        fprintf (stderr, "Can't open file %s\n", filename);
        return NULL;
    }

    CriuMagic magic = {};
    size_t err = fread (&magic, 1, sizeof (magic), file);
    if (err != sizeof (magic) || !CompareMagic (magic, expected_magic))
    {
        fprintf (stderr, "Bad magic in file %s.\n", filename);
        return NULL;
    }

    return file;
}

// type of unpacker's return value == type of *unpacked_image
// allocator for unpacker = default
int ReadMessage (MessageUnpacker unpacker, ProtobufCMessage** unpacked_image, FILE* file) 
{
    assert (unpacker);
    assert (unpacked_image);

    uint32_t size = 0;
    size_t err = fread (&size, 1,  sizeof (size), file);
    if (err != sizeof (size))
    {
        perror ("Can't read image size. Maybe bad file?");
        return -1;
    }

    uint8_t* packed_data = (uint8_t*) calloc (1, size);
    if (!packed_data)
    {   
        perror ("Can't allocate memory");
        return -1;
    }

    err = fread (packed_data, 1, size, file);
    if (err != size)
    {
        fprintf (stderr, "Can't read %u bytes as packed image. Maybe bad file?\n", size);
        free (packed_data);
        return -1;
    }

    *unpacked_image = unpacker (NULL, (size_t) size, packed_data);
    return *unpacked_image ? 0 : -1;
}

int ReadOnlyOneMessage (const char* path, const char* name, int pid, 
                        MessageUnpacker unpacker, ProtobufCMessage** unpacked_image, CriuMagic expected_magic)
{
    assert (path);
    assert (name);
    assert (unpacker);
    assert (unpacked_image);

    char* filename = (pid) ? CreateImagePathWithPid (path, name, pid) : CreateImagePath (path, name);
    if (!filename)
        return -1;

    FILE* file = StartImageReading (filename, expected_magic);
    int res = 0;

    if (!file)
        return -1;
    else
        res = ReadMessage (unpacker, unpacked_image, file);

    fclose (file);
    free   (filename);
    return res;
}

FILE* StartImageWriting (const char* filename, CriuMagic magic)
{
    assert (filename);

    FILE* file = fopen (filename, "w");
    if (!file)
    {
        fprintf (stderr, "Can't open file %s\n", filename);
        return NULL;
    }

    size_t err = fwrite (&magic, 1, sizeof (magic), file);
    if (err != sizeof (magic))
    {
        perror ("Write error");
        fclose (file);
        return NULL;
    }

    return file;
}

int WriteMessage (MessagePacker packer, const ProtobufCMessage* unpacked_image, size_t packed_image_size, FILE* file)
{
    assert (packer);
    assert (unpacked_image);
    assert (file);

    uint8_t* packed_image = (uint8_t*) calloc (1, packed_image_size);
    if (!packed_image)
    {
        perror ("Can't allocate memory");
        return -1;
    }

    packer (unpacked_image, packed_image);
    size_t err = fwrite (&packed_image_size, 1, sizeof (uint32_t), file); // very bad place, but it's only criu images standart
    err += fwrite (packed_image, 1, packed_image_size, file);

    if (err != packed_image_size + sizeof (uint32_t))
        perror ("Write error");

    free (packed_image);
    return err == packed_image_size + sizeof (uint32_t) ? 0 : -1;
}

int WriteOnlyOneMessage (const char* path, const char* name, int pid, 
                         MessagePacker packer, const void* unpacked_image, size_t packed_image_size, CriuMagic magic)
{
    assert (path);
    assert (name);
    assert (packer);
    assert (unpacked_image);

    char* filename = (pid) ? CreateImagePathWithPid (path, name, pid) : CreateImagePath (path, name);
    if (!filename)
        return -1;

    FILE* file = StartImageWriting (filename, magic);
    int res = 0;

    if (!file)
        return -1;
    else
        res = WriteMessage (packer, unpacked_image, packed_image_size, file);

    fclose (file);
    free   (filename);
    return res;
}
