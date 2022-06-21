#include <stdio.h>
#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/procfs.h>
#include <signal.h>
#include <compel/asm/fpu.h>
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
        GoPhdrs (elf, imgs);

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

    // Need empty struct
    assert (args->pstree  == NULL);
    assert (args->core    == NULL);
    assert (args->mm      == NULL);
    assert (args->pagemap == NULL);
    /*
        Need 3 types of arguments
        -c coredump path (elf file)
        -p path to criu images
        -i id of coredump on criu images
    */

   #define check_errors(arg_str, field) if (i_arg + 1 == argc)      \
                                        {                           \
                                            fprintf (stderr, "Error: after \"" "-i" "\" no arguments.\n"); \
                                            *args = EMPTY_ARGINFO;  \
                                            return 1;               \
                                        }                           \
                                        if (args->field)            \
                                        {                           \
                                            fprintf (stderr, "Error: \"" arg_str "\" field in args was typed yet.\n"); \
                                            *args = EMPTY_ARGINFO;  \
                                            return 1;               \
                                        }

    for (int i_arg = 1; i_arg < argc; i_arg++)
    {
        if (strcmp (argv[i_arg], "-c") == 0)
        {
            check_errors ("-c", elf);
            args->elf = argv[i_arg + 1];
            i_arg++;
        }

        else if (strcmp (argv[i_arg], "-p") == 0)
        {
            check_errors ("-p", criu_dump_path);
            args->criu_dump_path = argv[i_arg + 1];
            i_arg++;
        }
        
        else if (strcmp (argv[i_arg], "-i") == 0)
        {
            check_errors ("-i", criu_dump_id);
            args->criu_dump_id = argv[i_arg + 1];
            i_arg++;
        }
    }

    #define check_pointer(field, str)   if (args->field == NULL)                               \
                                        {                                                      \
                                            fprintf (stderr, "Error: no " str " in input.\n"); \
                                            *args = EMPTY_ARGINFO;                             \
                                            return 1;                                          \
                                        }

    check_pointer (criu_dump_path, "criu dump path");
    check_pointer (criu_dump_id, "criu dump pid");
    check_pointer (elf, "elf path");

    #undef check_pointer
    #undef check_errors
    return CreateImagesPath (args);
}

// C-style overloading

char* CreateOneImagePath  (const char* path, const char* name)
{
    char buffer[MAX_PATH_LEN] = "";
    sprintf (buffer, "%s/%s.img", path, name);
    return strdup (buffer);
}

char* CreateOneImagePathWithStrPid (const char* path, const char* name, const char* pid)
{
    char buffer[MAX_PATH_LEN] = "";
    sprintf (buffer, "%s/%s-%s.img", path, name, pid);
    return strdup (buffer);
}

char* CreateOneImagePathWithIntPid (const char* path, const char* name, int pid)
{
    char buffer[MAX_PATH_LEN] = "";
    sprintf (buffer, "%s/%s-%d.img", path, name, pid);
    return strdup (buffer);
}

int CreateImagesPath (ArgInfo* args)
{
    assert (args);
    assert (args->criu_dump_path);
    assert (args->criu_dump_id);

    #define check_pointer(field)    if (args->field == NULL)                                 \
                                    {                                                        \
                                        fprintf (stderr, "Error: Can't allocate memory.\n"); \
                                        ArgInfoFree (args);                                  \
                                        return 1;                                            \
                                    }

    // ToDo: check / on path? Why I wrote it?

    args->pstree  = CreateOneImagePath        (args->criu_dump_path, "pstree");
    args->core    = CreateOneImagePathWithStrPid (args->criu_dump_path, "core",    args->criu_dump_id);
    args->mm      = CreateOneImagePathWithStrPid (args->criu_dump_path, "mm",      args->criu_dump_id);
    args->pagemap = CreateOneImagePathWithStrPid (args->criu_dump_path, "pagemap", args->criu_dump_id);
    args->files   = CreateOneImagePath        (args->criu_dump_path, "files");

    check_pointer (pstree)
    check_pointer (core)
    check_pointer (mm)
    check_pointer (pagemap)
    check_pointer (files)
    
    #undef check_pointer
    return 0;
}

void ArgInfoFree (ArgInfo* args)
{
    assert (args);
    free (args->pstree);                        
    free (args->core);
    free (args->mm);
    free (args->pagemap);
    free (args->files);
    *args = EMPTY_ARGINFO;
    return;
}

Images* ImagesConstructor (ArgInfo* args)
{
    assert (args);
    assert (args->pstree);
    assert (args->core);
    assert (args->mm);
    assert (args->pagemap);

    #define check_pointer(pointer)  if (pointer == NULL)                                   \
                                    {                                                      \
                                        fprintf (stderr, "Error: Can't create images.\n"); \
                                        ArgInfoFree (args);                                \
                                        ImagesDestructor (imgs);                           \
                                        return NULL;                                       \
                                    }

    Images* imgs = (Images*) calloc (1, sizeof (*imgs));
    check_pointer (imgs);

    imgs->p_pstree  = (PackedImage*) ReadFile (args->pstree);
    imgs->p_core    = (PackedImage*) ReadFile (args->core);
    imgs->p_mm      = (PackedImage*) ReadFile (args->mm);
    imgs->p_pagemap = (PackedImage*) ReadFile (args->pagemap);
    imgs->p_files   = (PackedImage*) ReadFile (args->files);

    check_pointer (imgs->p_pstree);    
    check_pointer (imgs->p_core);
    check_pointer (imgs->p_mm);
    check_pointer (imgs->p_pagemap);
    check_pointer (imgs->p_files);

    imgs->pstree  = pstree_entry__unpack  (NULL, imgs->p_pstree->size0,  &(imgs->p_pstree->pb_msg));
    imgs->core    = core_entry__unpack    (NULL, imgs->p_core->size0,    &(imgs->p_core->pb_msg));
    imgs->mm      = mm_entry__unpack      (NULL, imgs->p_mm->size0,      &(imgs->p_mm->pb_msg));
    imgs->pagemap = pagemap_entry__unpack (NULL, imgs->p_pagemap->size0, &(imgs->p_pagemap->pb_msg));
    imgs->files   = file_entry__unpack    (NULL, imgs->p_files->size0,   &(imgs->p_files->pb_msg));

    #undef check_pointer
    return imgs;
}

int WritePackedImage (PackedImage* img, const char* path, const char* name, int pid) // If pid not needeed, pid = 0
{
    char* filename = NULL;
    
    if (pid == 0)
        filename = CreateOneImagePath (path, name);
    else
        filename = CreateOneImagePathWithIntPid (path, name, pid);

    int res = WriteFile (filename, (const char*) img, img->size0 + SIZEOF_P_IMAGE_HDR);
    free (filename);
    return res;
}

// In this program pid's are keeping with this strange types. Sorry for strange args.
void ChangeImagePid (const char* path, const char* name, const char* old_pid, int new_pid)
{
    const char* old_name = CreateOneImagePathWithStrPid (path, name, old_pid);
    const char* new_name = CreateOneImagePathWithIntPid (path, name, new_pid);
    rename (old_name, new_name);
    free (old_name);
    free (new_name);
    return;
}

void ImagesWrite (Images* imgs, ArgInfo* args)
{
    assert (imgs);
    assert (args->pstree);
    assert (args->core);
    assert (args->mm);
    assert (args->pagemap);
    assert (imgs->pstree);
    assert (imgs->core);
    assert (imgs->mm);
    assert (imgs->pagemap);

    pstree_entry__pack  (imgs->pstree,  &(imgs->p_pstree->pb_msg));
    core_entry__pack    (imgs->core,    &(imgs->p_core->pb_msg));
    mm_entry__pack      (imgs->mm,      &(imgs->p_mm->pb_msg));
    pagemap_entry__pack (imgs->pagemap, &(imgs->p_pagemap->pb_msg));
    file_entry__pack    (imgs->files,   &(imgs->p_files->pb_msg));

    // ToDo: write all structs from images array

    WritePackedImage (imgs->p_pstree,  args->criu_dump_path, "pstree",  0);
    WritePackedImage (imgs->p_core,    args->criu_dump_path, "core",    imgs->pstree->pid);
    WritePackedImage (imgs->p_mm,      args->criu_dump_path, "mm",      imgs->pstree->pid);
    WritePackedImage (imgs->p_pagemap, args->criu_dump_path, "pagemap", imgs->pstree->pid);
    WritePackedImage (imgs->p_files,   args->criu_dump_path, "files",   0);

    char *old_name = NULL, *new_name = NULL;

    // In my images I found 2 files, that need to be renamed: fs and ids.
    // core, mm and pagemap are renamed yet.
    // ToDo: find all files in documentation.

    ChangeImagePid (args->criu_dump_path, "fs",  args->criu_dump_id, imgs->pstree->pid);
    ChangeImagePid (args->criu_dump_path, "ids", args->criu_dump_id, imgs->pstree->pid);

    return;
}

void ImagesDestructor (Images* imgs)
{
    if (!imgs)
        return;

    pstree_entry__free_unpacked  (imgs->pstree,  NULL);
    core_entry__free_unpacked    (imgs->core,    NULL);
    mm_entry__free_unpacked      (imgs->mm,      NULL);
    pagemap_entry__free_unpacked (imgs->pagemap, NULL);
    file_entry__free_unpacked    (imgs->files,   NULL);

    free (imgs->p_pstree);
    free (imgs->p_core);
    free (imgs->p_mm);
    free (imgs->p_pagemap);
    free (imgs->p_files);

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

    for (size_t i_phdr = 0; i_phdr < elf->phnum; i_phdr++)
    {
        switch (elf->phdr_table[i_phdr].p_type)
        {
            case PT_NOTE:
                GoNhdrs (elf->buf + elf->phdr_table[i_phdr].p_paddr, elf->phdr_table[i_phdr].p_filesz, imgs);
                break;

            // ToDo: case PT_LOAD:

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
    size_t align = 0, common_align = 0;
    
    while (common_align < p_filesz)
    {
        switch (((Elf_Nhdr*) nhdrs)->n_type)
        {
            // ToDo: auto align and return err status

            case NT_PRPSINFO:
                align = GoPrpsinfo ((Elf_Nhdr*) nhdrs, imgs);
                break;
            
            case NT_PRSTATUS:
                align = GoPrstatus ((Elf_Nhdr*) nhdrs, imgs);
                break;

            case NT_FPREGSET:
                align = GoFpregset ((Elf_Nhdr*) nhdrs, imgs);
                break;

            case NT_X86_XSTATE:
                align = GoX86_State ((Elf_Nhdr*) nhdrs, imgs);
                break;

            case NT_SIGINFO:
                align = GoSiginfo ((Elf_Nhdr*) nhdrs, imgs);
                break;

            case NT_AUXV:
                align = GoAuxv ((Elf_Nhdr*) nhdrs, imgs);
                break;

            case NT_FILE:
                align = GoFile ((Elf_Nhdr*) nhdrs, imgs);
                break;

            default:
                // ToDo: Give more debug info
                fprintf (stderr, "Warning: I don't know, how to parse note header with type %u.\n", 
                        ((Elf_Nhdr*) nhdrs)->n_type);
                break;
        }

        if (!align)
            return;
        nhdrs += align;
        common_align += align;
    }
    
    return;
}

#define ERR_NOT_CREATED assert (nhdr); \
                        fprintf (stderr, "Error: Function %s isn't created yet.\n", __func__); \
                        return 0

/*
    For note functions:
    Note = {Nhdr, name, desc}

    Name and desc have align = 4
*/

// ToDo: Check name?
// Note for offset: align of name = 4
#define NHDR_START(type, name)  assert (nhdr);                                                                             \
                                assert (imgs);                                                                             \
                                assert (nhdr->n_type == type);                                                             \
                                                                                                                           \
                                if (nhdr->n_descsz != sizeof (name##_t))                                                   \
                                    fprintf (stderr, "Error: Bad n_descsz in %s:\n"                                        \
                                                     "Expected: sizeof (" #name ") = %lu\n"                                \
                                                     "Detected: %u\n", __func__, sizeof (name##_t), nhdr->n_descsz);       \
                                                                                                                           \
                                size_t offset = sizeof (*nhdr) + (nhdr->n_namesz + 3) % 4;                                 \
                                name##_t* name = (name##_t*) (((void*) nhdr) + offset)

// align of desc = 4
#define NHDR_RETURN return offset + (nhdr->n_descsz + 3) % 4

size_t GoPrpsinfo (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_PRPSINFO, prpsinfo);

    #define TASK_ALIVE 0x1
    #define TASK_DEAD  0x2
    #define TASK_STOPPED 0x3

    switch (prpsinfo->pr_state)
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

    imgs->core->thread_core->sched_prio = prpsinfo->pr_nice; // ToDo: sched_prio not in thread_core?
    imgs->core->tc->flags = prpsinfo->pr_flag;
    imgs->core->thread_core->creds->uid = prpsinfo->pr_uid;
    imgs->core->thread_core->creds->gid = prpsinfo->pr_gid;
    imgs->pstree->pid  = prpsinfo->pr_pid;
    imgs->pstree->ppid = prpsinfo->pr_ppid;
    imgs->pstree->pgid = prpsinfo->pr_pgrp;
    imgs->pstree->sid  = prpsinfo->pr_sid;

    // ToDo: Psarg - it's command line, working with memory and chunks.
    // prpsinfo->pr_psargs;

    free (imgs->core->tc->comm);
    imgs->core->tc->comm = strdup (prpsinfo->pr_fname);

    #undef TASK_ALIVE
    #undef TASK_DEAD
    #undef TASK_STOPPED
    NHDR_RETURN;
}

size_t GoPrstatus (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_PRSTATUS, prstatus);

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
    NHDR_START (NT_FPREGSET, elf_fpregset);

    UserX86FpregsEntry* regs = imgs->core->thread_info->fpregs;
    regs->cwd = elf_fpregset->cwd;
    regs->swd = elf_fpregset->swd;
    regs->twd = elf_fpregset->ftw;
    regs->fop = elf_fpregset->fop;
    regs->rip = elf_fpregset->rip;
    regs->rdp = elf_fpregset->rdp;
    regs->mxcsr = elf_fpregset->mxcsr;
    regs->mxcsr_mask = elf_fpregset->mxcr_mask;
    memcpy (regs->st_space,  elf_fpregset->st_space,  sizeof (elf_fpregset->st_space));
    memcpy (regs->xmm_space, elf_fpregset->xmm_space, sizeof (elf_fpregset->xmm_space));
    
    NHDR_RETURN;
}

size_t GoX86_State (Elf_Nhdr* nhdr, Images* imgs)
{
    typedef struct xsave_struct xsave_t;
    NHDR_START (NT_X86_XSTATE, xsave);

    UserX86FpregsEntry* regs = imgs->core->thread_info->fpregs;
    regs->twd = xsave->i387.twd;
    // ToDo: st_space, xmm_space as in previos function?

    if (regs->xsave == NULL)
    {
        fprintf (stderr, "Warning: UserX86XsaveEntry doesn't exist yet. Creating it...\n");
        regs->xsave = (UserX86XsaveEntry*) calloc (1, sizeof (*(regs->xsave)));
    }

    regs->xsave->xstate_bv = xsave->xsave_hdr.xstate_bv;
    free (regs->xsave->ymmh_space);
    regs->xsave->ymmh_space = (uint32_t*) calloc (1, sizeof (xsave->ymmh));
    memcpy (regs->xsave->ymmh_space, &(xsave->ymmh), sizeof (xsave->ymmh));

    NHDR_RETURN;
}

size_t GoSiginfo (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_SIGINFO, siginfo);
    static size_t new_pos = 0;
    // ToDo: new_pos < n_signals after working program?
    if (new_pos >= imgs->core->tc->signals_s->n_signals)
    {
        fprintf (stderr, "Error: amount of siginfo_t structs >= n_signals. Maybe fix it in this program?\n");
        return 0;
    }

    // ToDo: Is it correct? Here was:
    // memcpy (imgs->core->tc->signals_s->signals[new_pos]->siginfo.data, siginfo->si_code, nhdr->n_descsz);
    // But it's incorrect. I don't know, what I meant when I wrote this one year ago.
    memcpy (imgs->core->tc->signals_s->signals[new_pos]->siginfo.data, &(siginfo->si_code), nhdr->n_descsz);

    new_pos++;
    NHDR_RETURN;
}

size_t GoAuxv (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_AUXV, Elf_auxv);

    free (imgs->mm->mm_saved_auxv);
    imgs->mm->mm_saved_auxv = (uint64_t*) calloc (1, nhdr->n_descsz + 1);
    memcpy (imgs->mm->mm_saved_auxv, Elf_auxv, nhdr->n_descsz); // ToDo: check n_descsz % 16 == 0?

    NHDR_RETURN;
}

// ToDo: where get file_t?
struct file_array
{
    long start, end, file_ofs;
};

typedef struct
{
    long count, pagesize;
    struct file_array array[]; // As my tests show, it is correct.
} file_t;

size_t GoFile (Elf_Nhdr* nhdr, Images* imgs)
{
    NHDR_START (NT_FILE, file);

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

    NHDR_RETURN;
}

#undef NHDR_START
#undef NHDR_RETURN
#undef ERR_NOT_CREATED
