#include <stdio.h>
#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <sys/procfs.h>
#include "Images/core.pb-c.h"
#include "Images/pstree.pb-c.h"
#include "Images/mm.pb-c.h"
#include "Images/pagemap.pb-c.h"
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

int ParseArguments (int argc, char** argv, ArgInfo* args)
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

    // ToDo: check / on path?

    #define create_path_img(field)  sprintf (buffer, "%s/%s-%s.img", args->criu_dump_path, #field, args->criu_dump_id); \
                                    args->field = strdup (buffer); \
                                    check_pointer (field)

    char buffer[MAX_PATH_LEN] = "";
    
    // ToDo: Other formats of names of images? 
    sprintf (buffer, "%s/pstree.img", args->criu_dump_path);
    args->pstree = strdup (buffer);
    check_pointer(pstree);

    create_path_img (core);
    create_path_img (mm);
    create_path_img (pagemap);

    #undef create_path_img
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

    check_pointer (imgs->p_pstree);    
    check_pointer (imgs->p_core);
    check_pointer (imgs->p_mm);
    check_pointer (imgs->p_pagemap);

    imgs->pstree  = pstree_entry__unpack  (NULL, imgs->p_pstree->size0,  &(imgs->p_pstree->pb_msg));
    imgs->core    = core_entry__unpack    (NULL, imgs->p_core->size0,    &(imgs->p_core->pb_msg));
    imgs->mm      = mm_entry__unpack      (NULL, imgs->p_mm->size0,      &(imgs->p_mm->pb_msg));
    imgs->pagemap = pagemap_entry__unpack (NULL, imgs->p_pagemap->size0, &(imgs->p_pagemap->pb_msg));

    #undef check_pointer
    return imgs;
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

    // ToDo: write only first struct in array images?
    WriteFile (args->pstree,  (const char*) imgs->p_pstree,  imgs->p_pstree->size0 + SIZEOF_P_IMAGE_HDR);
    WriteFile (args->core,    (const char*) imgs->p_core,    imgs->p_core->size0 + SIZEOF_P_IMAGE_HDR);
    WriteFile (args->mm,      (const char*) imgs->p_mm,      imgs->p_mm->size0 + SIZEOF_P_IMAGE_HDR);
    WriteFile (args->pagemap, (const char*) imgs->p_pagemap, imgs->p_pagemap->size0 + SIZEOF_P_IMAGE_HDR);

    char old_name[MAX_PATH_LEN] = "";
    char new_name[MAX_PATH_LEN] = "";

    // In my images I found 2 files, that need to be renamed: fs and ids
    // ToDo: find all files in documentation.

    // Sorry for copypaste, I don't want to write define for 2 files.

    sprintf (old_name, "%s/fs-%s.img", args->criu_dump_path, args->criu_dump_id);
    sprintf (new_name, "%s/fs-%d.img", args->criu_dump_path, imgs->pstree->pid);
    rename (old_name, new_name);

    sprintf (old_name, "%s/ids-%s.img", args->criu_dump_path, args->criu_dump_id);
    sprintf (new_name, "%s/ids-%d.img", args->criu_dump_path, imgs->pstree->pid);
    rename (old_name, new_name);

    return;
}

void ImagesDestructor (Images* imgs)
{
    if (!imgs)
        return;

    pstree_entry__free_unpacked  (imgs->pstree, NULL);
    core_entry__free_unpacked    (imgs->core, NULL);
    mm_entry__free_unpacked      (imgs->mm, NULL);
    pagemap_entry__free_unpacked (imgs->pagemap, NULL);

    free (imgs->p_pstree);
    free (imgs->p_core);
    free (imgs->p_mm);
    free (imgs->p_pagemap);

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
    size_t align = 0;
    
    while (align < p_filesz)
    {
        switch (((Elf_Nhdr*) nhdrs)->n_type)
        {
            // ToDo: Create arrays and write after?

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

size_t GoPrpsinfo  (Elf_Nhdr* nhdr, Images* imgs)
{
    assert (nhdr);
    assert (nhdr->n_type == NT_PRPSINFO);

    if (nhdr->n_descsz != sizeof (prpsinfo_t))
        fprintf (stderr, "Error: Bad n_descsz in %s:\n"
                         "Expected: sizeof (prpsinfo) = %lu\n"
                         "Detected: %u\n", __func__, sizeof (prpsinfo_t), nhdr->n_descsz);

    // ToDo: Check name?

    size_t offset = sizeof (*nhdr) + (nhdr->n_namesz + 3) % 4; // align of name = 4
    prpsinfo_t* prpsinfo = (prpsinfo_t*) (((void*) nhdr) + offset);

    return offset + (nhdr->n_descsz + 3) % 4; // align of desc = 4
}

size_t GoPrstatus  (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

size_t GoFpregset  (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

size_t GoX86_State (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

size_t GoSiginfo   (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

size_t GoAuxv      (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

size_t GoFile       (Elf_Nhdr* nhdr, Images* imgs)
{
    ERR_NOT_CREATED;
}

#undef ERR_NOT_CREATED
