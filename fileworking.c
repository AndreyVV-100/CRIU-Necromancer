#include <stdio.h>
#include <elf.h>
#include <malloc.h>
#include <assert.h>
#include <errno.h>
#include "fileworking.h"

char* ReadFile (const char* filename)
{
    assert (filename);

    FILE* file = fopen (filename, "rb");
    if (!file)
    {
        fprintf (stderr, "Error: Unable to open file %s : no such file or directory.\n", filename);
        return NULL;
    }

    long num_bytes = GetFileSize (file);
    if (num_bytes == -1)
        return NULL;

    char* buf = calloc (num_bytes + 1, sizeof (*buf));
	if (!buf)
	{
		fprintf (stderr, "Error: Unable to allocate memory\n");
        fclose (file);
		return 0;
	}

	if (fread (buf, sizeof(*buf), num_bytes, file) != (size_t) num_bytes)
    {
        fprintf (stderr, "Error: During reading something went wrong...\n");
	    free (buf);
        buf = NULL;
    }

    fclose (file);
    return buf;
}

long GetFileSize (FILE* file)
{
    // ToDo: Work with files, that pointer != start
    assert (file);

    fseek (file, 0, SEEK_END);
	long num_bytes = ftell (file); // ToDo: returns -1 error fix
    if (num_bytes == -1L)
    {
        fprintf (stderr, "Error: ftell returns -1, error code: %d.\n", errno);
        return -1;
    }

	fseek (file, 0, SEEK_SET);
    return num_bytes;
}

int WriteFile (const char* filename, const char* buf, size_t buf_size)
{
    assert (filename);
    assert (buf);
    assert (buf_size);

    FILE* file = fopen (filename, "wb");
    if (!file)
    {
        fprintf (stderr, "Error: Unable to open file %s : no such file or directory.\n", filename);
        return 1;
    }

    fwrite (buf, sizeof (*buf), buf_size, file);
    fclose (file);
    return 0;
}
