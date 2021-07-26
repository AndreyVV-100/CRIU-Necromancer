#include <stdio.h>
#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include "fileworking.h"

char* ReadFile (const char* filename)
{
    assert (filename);

    FILE* file = fopen (filename, "rb");
    if (!file)
    {
        printf ("Error: Unable to open file %s : no such file or directory.\n", filename);
        return NULL;
    }

    size_t num_symbols = GetFileSize (file);
    char* buf = calloc (num_symbols + 1, sizeof (*buf));

	if (!buf)
	{
		printf ("Error: Unable to allocate memory\n");
        fclose (file);
		return 0;
	}

	if (fread (buf, sizeof(*buf), num_symbols, file) != num_symbols)
    {
        printf ("Error: During reading something went wrong...\n");
	    free (buf);
        buf = NULL;
    }

    fclose (file);
    return buf;
}

size_t GetFileSize (FILE* file)
{
    // ToDo: Work with files, that pointer != start
    assert (file);

    fseek (file, 0, SEEK_END);
	size_t num_symbols = ftell (file); // ToDo: returns -1 error fix
	fseek (file, 0, SEEK_SET);

    return num_symbols;
}

