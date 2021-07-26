#ifdef MODE32

    #define Elf_Half    Elf32_Half
    #define Elf_Sword   Elf32_Sword
    #define Elf_Word    Elf32_Word
    #define Elf_Xword   Elf32_Xword
    #define Elf_Sxword  Elf32_Sxword
    #define Elf_Addr    Elf32_Addr
    #define Elf_Off     Elf32_Off
    #define Elf_Section Elf32_Section
    #define Elf_Versym  Elf32_Versym
    #define Elf_Ehdr    Elf32_Ehdr
    #define Elf_Shdr    Elf32_Shdr
    #define Elf_Chdr    Elf32_Chdr
    #define Elf_Sym     Elf32_Sym
    #define Elf_Syminfo Elf32_Syminfo
    #define Elf_Rel     Elf32_Rel
    #define Elf_Rela    Elf32_Rela
    #define Elf_Phdr    Elf32_Phdr
    #define Elf_Dyn     Elf32_Dyn
    #define Elf_Verdef  Elf32_Verdef
    #define Elf_Verdaux Elf32_Verdaux
    #define Elf_Verneed Elf32_Verneed
    #define Elf_Vernaux Elf32_Vernaux
    #define Elf_auxv_t  Elf32_auxv_t
    #define Elf_Nhdr    Elf32_Nhdr
    #define Elf_Move    Elf32_Move
    #define Elf_Lib     Elf32_Lib

#else

    #define Elf_Half    Elf64_Half
    #define Elf_Sword   Elf64_Sword
    #define Elf_Word    Elf64_Word
    #define Elf_Xword   Elf64_Xword
    #define Elf_Sxword  Elf64_Sxword
    #define Elf_Addr    Elf64_Addr
    #define Elf_Off     Elf64_Off
    #define Elf_Section Elf64_Section
    #define Elf_Versym  Elf64_Versym
    #define Elf_Ehdr    Elf64_Ehdr
    #define Elf_Shdr    Elf64_Shdr
    #define Elf_Chdr    Elf64_Chdr
    #define Elf_Sym     Elf64_Sym
    #define Elf_Syminfo Elf64_Syminfo
    #define Elf_Rel     Elf64_Rel
    #define Elf_Rela    Elf64_Rela
    #define Elf_Phdr    Elf64_Phdr
    #define Elf_Dyn     Elf64_Dyn
    #define Elf_Verdef  Elf64_Verdef
    #define Elf_Verdaux Elf64_Verdaux
    #define Elf_Verneed Elf64_Verneed
    #define Elf_Vernaux Elf64_Vernaux
    #define Elf_auxv_t  Elf64_auxv_t
    #define Elf_Nhdr    Elf64_Nhdr
    #define Elf_Move    Elf64_Move
    #define Elf_Lib     Elf64_Lib

#endif


typedef struct
{
    char* buf;
    Elf_Ehdr* elf_hdr; // usually == buf
    Elf_Phdr* phdr_table;
    Elf_Half phnum;
} Elf;


Elf* ElfConstructor (const char* filename);

void ElfDestructor (Elf* elf);

Elf_Ehdr* CheckElfHdr (const char* buf);

Elf_Phdr* CheckPhdrs (Elf* elf);
