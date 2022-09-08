#include "Images/core.pb-c.h"
#include "Images/pstree.pb-c.h"
#include "Images/mm.pb-c.h"
#include "Images/pagemap.pb-c.h"
#include "Images/fdinfo.pb-c.h"

#include <stdio.h>
#include "user.h"
#include "file.h"

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
    // Placed on argv
    const char* elf;
    const char* criu_dump_path;

    // Given from pstree
    int criu_dump_id;
} ArgInfo;

typedef struct
{
    char* buf;
    Elf_Ehdr* elf_hdr; // usually == buf
    Elf_Phdr* phdr_table;
    Elf_Half phnum;
} Elf;

/*
    As I understand, in v1.1 always 2 magics, except: inventory.
    From https://criu.org/Images:
    Or, you can visualize it like

    Type 	    Size, bytes
    Magic0 	    4
    [Magic1]    [4]
    Size0 	    4
    Message0 	Size0
    ... 	    ...
    SizeN 	    4
    MessageN    SizeN 

    Such images can be one of

    Array image files
        In these files the amount of entries can be any. 
        You should read the image file up to the EOF to find out the exact number.

    Single-entry image files
        In these files exactly one entry is stored.

    name    type
    pstree  array
    core    single-entry
    mm 	    single-entry

    Pagemap files. {...} The file is a set of protobuf messages.

    // ToDo: pstree and pagemap work with array?
*/

typedef struct
{
    PstreeEntry* pstree;
    CoreEntry* core;
    MmEntry* mm;
    FILE *pagemap, *pages;
    // PagemapEntry* pagemap; // pagemap[0] = pages_id; pages-id.img - raw data
    // FileEntry* files;

    // int reserved;
} Images; 
// ToDo: I don't like this struct and working with it. It's look like big copypaste.

typedef ProtobufCMessage*  MessageUnpacker (ProtobufCAllocator*, size_t, const uint8_t*); // ToDo: maybe void -> ProtobufCMessage?
typedef size_t MessagePacker (const void*, uint8_t*);
typedef struct
{
    uint32_t magic0, magic1;
} CriuMagic;

// MY_ will useful after, when include criu/criu/include/magic.h
// All numbers are given from this header
static const CriuMagic MY_PSTREE_MAGIC  = {0x54564319, 0x50273030};
static const CriuMagic MY_CORE_MAGIC    = {0x54564319, 0x55053847};
static const CriuMagic MY_MM_MAGIC      = {0x54564319, 0x57492820};
static const CriuMagic MY_PAGEMAP_MAGIC = {0x54564319, 0x56084025};
static const CriuMagic MY_FILES_MAGIC   = {0x54564319, 0x56213732};

static inline int CompareMagic (CriuMagic a, CriuMagic b) {return a.magic0 == b.magic0 && a.magic1 == b.magic1;}

const size_t SIZEOF_P_IMAGE_HDR = sizeof (uint32_t) * 3; // not including pb_msg
const ArgInfo EMPTY_ARGINFO = {};
const Images  EMPTY_IMAGES  = {};
const size_t PAGESIZE = 4096;

#define PE_PRESENT (1 << 2) // copypasted from criu/include/pagemap.h
#define VMA_AREA_VSYSCALL (1 << 2) // copypasted from criu/include/image.h
#define VMA_AREA_HEAP (1 << 5) // copypasted from criu/include/image.h

#define MAX_PATH_LEN 1024

int ParseArguments (int argc, char** argv, ArgInfo* args);

// C-style overloading. Maybe write it the other way?
char* CreateImagePath           (const char* path, const char* name);
char* CreateImagePathWithPid (const char* path, const char* name, int pid);

void ArgInfoFree (ArgInfo* args);

Images* ImagesConstructor (ArgInfo* args);
void ImagesDestructor (Images* imgs);

void ChangeImagePid (const char* path, const char* name, int old_pid, int new_pid);
void ImagesWrite (Images* imgs, ArgInfo* args);

Elf* ElfConstructor (const char* filename);
void ElfDestructor (Elf* elf);

Elf_Ehdr* CheckElfHdr (const char* buf);
Elf_Phdr* CheckPhdrs (Elf* elf);

void GoPhdrs (Elf* elf, Images* imgs);

void GoNhdrs (void* nhdrs, Elf_Xword p_filesz, Images* imgs);

// ToDo: Mini documentation
/*
    ToDo: Maybe do this: many headers ->  one image
          Instead of:    one  header  -> many images
    ???
*/

static inline size_t GetAligned (size_t value, size_t align)
{
    return value + (align - value % align) % align;
}
#define GetAlignedSimple(value) GetAligned (value, sizeof (value))

size_t GoPrpsinfo  (Elf_Nhdr* nhdr, Images* imgs);
size_t GoPrstatus  (Elf_Nhdr* nhdr, Images* imgs);
size_t GoFpregset  (Elf_Nhdr* nhdr, Images* imgs);
size_t GoX86_State (Elf_Nhdr* nhdr, Images* imgs);
size_t GoSiginfo   (Elf_Nhdr* nhdr, Images* imgs);
size_t GoAuxv      (Elf_Nhdr* nhdr, Images* imgs);
size_t GoFile      (Elf_Nhdr* nhdr, Images* imgs);

char* GetFilenameByNumInNTFile (file_t* file, size_t file_num);

int GoLoadPhdr (Elf* elf, Elf_Phdr* phdr, Images* imgs, size_t vma_counter);
void MmChangeIfNeeded (MmEntry* mm, VmaEntry* vma, Elf_Phdr* phdr);
uint32_t GetVmaProtByPhdr (Elf_Word phdr_flags);

// type of unpacker's return value == type of *unpacked_image
// allocator for unpacker = default
FILE* StartImageReading (const char* filename, CriuMagic expected_magic);
int ReadMessage (MessageUnpacker unpacker, ProtobufCMessage** unpacked_image, FILE* file);
int ReadOnlyOneMessage (const char* path, const char* name, int pid, 
                                 MessageUnpacker unpacker, ProtobufCMessage** unpacked_image, CriuMagic expected_magic);

FILE* StartImageWriting (const char* filename, CriuMagic magic);
int WriteMessage (MessagePacker packer, const ProtobufCMessage* unpacked_image, size_t packed_image_size, FILE* file);
int WriteOnlyOneMessage (const char* path, const char* name, int pid, 
                         MessagePacker packer, const void* unpacked_image, size_t packed_image_size, CriuMagic magic);
/*  
    ToDo: 
        -threads, many processes
        -files
        -arg, env, stack

    Check list:

    -core - core_entry in core.proto:
            -mtype - supported only x86-64 (but isn't checked) - ?
            -thread-info:
                -gpregs - in GoPrstatus - OK
                -fpregs - in GoFpregset - OK
            -tc:
                -Some in GoPrpsinfo - OK
                -Some in GoSiginfo  - OK
                -Some required fields isn't changing (exit_code, personality) - OK

    -files - in zero approximation - skip. ToDo

    -ids - only renaming, but no working with it.

    -pstree - pstree_entry in pstree.proto:
                -all in GoPrpsinfo - OK (but no threads)

    -mm - mm_entry in mm.proto:
            -mm_saved_auxv - working in GoAuxv - OK
            -vmas - OK, but I hope thas phdrs <==> vmas (and pages too)
            -mm_arg, mm_env, mm_stack - Now is calculated by simple way. In other situations very hard, ToDo
            -mm_brk, mm_code, mm_data - simple writing it

    -pagemap - OK, but I hope thas phdrs <==> pages (and vmas too)

    -timens - responsible for time, skipping

    -tty-info - no working with it in coredump.py, skipping (see https://github.com/checkpoint-restore/criu/blob/criu-dev/coredump/criu_coredump/coredump.py)

*/
