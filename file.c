#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file.h"

static void map_file_into_array(file_t *file, uint8_t *buffer)
{
    line_t *tmp = calloc(1, sizeof(*tmp));
    if (tmp == NULL)
        abort();

    size_t curr_lineno = 0, alloc_cap = 1;
    size_t i = 0; /* For iterating byte-by-byte in buffer */

    while (1) {
        size_t j = 0; /* For column result */
        size_t k = i + 1; /* Begining of current buffer position */
        do {
            i++; j++;
            if (buffer[i] == FILE_EOF || buffer[i] == '\n')
                break;
        } while (1);
        if (curr_lineno >= alloc_cap) {
            alloc_cap *= 2;
            tmp = realloc(tmp, alloc_cap * sizeof(tmp[0]));
            if (tmp == NULL)
                abort();
            memset(&tmp[curr_lineno], 0, sizeof(tmp[0]));
        }
        if (buffer[i] == FILE_EOF) {
            tmp[curr_lineno].buffer = NULL;
            tmp[curr_lineno].is_eof = 1;
            break;
        } else {
            uint8_t *tmp_buf = calloc(1, j + 1);
            if (tmp_buf == NULL)
                abort();
            memcpy(tmp_buf, &buffer[k], j);
            tmp_buf[j] = 0;
            tmp[curr_lineno].buffer = tmp_buf;
            tmp[curr_lineno].cur = 0;
            tmp[curr_lineno].len = j;
            tmp[curr_lineno].is_eof = 0;
        }
        curr_lineno++;
    }

    file->source.lines = tmp;
    file->source.cur = 0;
    file->source.len = curr_lineno;
}

/* Return value:
 *  -1: error.
 *   0: file content successfuly loaded into memory but no newline found.
 *   1: ok, loaded into memory and newline found.
 */
int file_read(file_t *file, const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd == -1)
        return -1;

    struct stat sb;
    if (fstat(fd, &sb) == -1)
        goto close_file;

    if (!S_ISREG(sb.st_mode))
        goto close_file;

    size_t size = (size_t)sb.st_size;
    if (size == 0)
        goto close_file;

    uint8_t *buffer = calloc(1ul, size + 2ul); /* <NL> + <EOF> */
    if (buffer == NULL)
        goto close_file;

    read(fd, buffer, size);
    file->size = size;
    file->path = path;

    int rc = 1, eof_at = 0; /* ok */

    if (buffer[size - 1] == '\n') {
        if (buffer[size - 2] == '\r') { /* "\r\n" -> "\nEOF" */
            buffer[size - 2] = '\n';
            eof_at = -1;
        } /* else only '\n', nothing to do... */
    } else if (buffer[size - 1] == '\r') {
        if (buffer[size - 2] == '\n') { /* "\n\r" -> "\nEOF" */
            buffer[size - 2] = '\n';
            eof_at = -1;
        } else {
            buffer[size - 1] = '\n';
        }
    } else { /* no newline found */
        buffer[size] = '\n';
        eof_at = 1;
        rc = 0;
    }

    buffer[size + eof_at] = FILE_EOF;

    close(fd);
    map_file_into_array(file, buffer);
    free(buffer);

    return rc;

close_file:
    close(fd);
    return -1;
}

void file_close(file_t *file)
{
    if (file != NULL) {
        for (size_t i = 0; i < file->source.len; i++)
            free(file->source.lines[i].buffer);
        free(file->source.lines);
    }
}
