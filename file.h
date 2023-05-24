#ifndef NKCC_FILE_H
#define NKCC_FILE_H

#define FILE_EOF    255u

typedef struct {
    uint8_t *buffer;
    size_t cur;
    size_t len;
    int is_eof;
} line_t;

typedef struct {
    line_t *lines;
    size_t cur;
    size_t len;
} source_t;

typedef struct {
    source_t source;
    const char *path;
    size_t size;
} file_t;

/* Return value:
 *  -1: error.
 *   0: file content successfuly loaded into memory but no newline found.
 *   1: ok, loaded into memory and newline found.
 */
int file_read(file_t *file, const char *path);
void file_close(file_t *file);

#endif // NKCC_FILE_H
