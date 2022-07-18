/*
 * Format of NT_FILE note:
 *
 * long count     -- how many files are mapped
 * long page_size -- units for file_ofs
 * array of [COUNT] elements of
 *   long start
 *   long end
 *   long file_ofs
 * followed by COUNT filenames in ASCII: "FILE1" NUL "FILE2" NUL...
 */

struct file_array
{
    long start, end, file_ofs;
};

// in linux kernel this struct doesn't exist, it's only array of longs
// Some more info you can find here: https://elixir.bootlin.com/linux/latest/source/fs/binfmt_elf.c#L1633
typedef struct
{
    long count, pagesize;
    struct file_array array[]; // As my tests show, it is correct.
    // after that names are placed, but I don't know, how to show it in this struct
} file_t;
