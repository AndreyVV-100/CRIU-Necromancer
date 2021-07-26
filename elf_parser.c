#include <stdio.h>
#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include "elf_parser.h"
#include "fileworking.h"

int main(int argc, char** argv)
{
    if (argc == 1)
    {
        printf ("Error: No input file.\n"); // ToDo: help
        return 0;
    }

    Elf* my_elf = ElfConstructor (argv[1]);
    ElfDestructor (my_elf);
    return 0;
}

Elf* ElfConstructor (const char* filename)
{
    assert (filename);
    Elf* elf_result = (Elf*) calloc (1, sizeof (*elf_result)); // calloc <==> all pointers are NULL 

    #define check_pointer(name) if (!(elf_result->name))        \
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

    size_t check_no = 1;
    #define check(condition) if (!(condition)) \
                             {                 \
                                 printf ("Error: Bad elf file. Check number: %lu\n", check_no); \
                                 return NULL;  \
                             }                 \
                             else              \
                                check_no++;

    // ToDo: or check (strncmp (elf_hdr->e_ident, ELFMAG) == 0)?
    check (elf_hdr->e_ident[0] == ELFMAG0);
    check (elf_hdr->e_ident[1] == ELFMAG1);
    check (elf_hdr->e_ident[2] == ELFMAG2);
    check (elf_hdr->e_ident[3] == ELFMAG3);
    check (elf_hdr->e_ident[4] == ELFCLASS32 || elf_hdr->e_ident[4] == ELFCLASS64);
    check (elf_hdr->e_ident[6] == EV_CURRENT)

    check (elf_hdr->e_type == ET_CORE); // WARNING: only for my task
    check (elf_hdr->e_machine != EM_NONE);
    check (elf_hdr->e_version == EV_CURRENT);

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
        if (elf->phdr_table[i_phdr].p_filesz < elf->phdr_table[i_phdr].p_memsz)
        {
            printf ("Error: Bad program header number %d.\n", i_phdr);
            elf->phdr_table = NULL;
            elf->phnum = 0;
            break;
        }
    }

    return elf->phdr_table;
}
