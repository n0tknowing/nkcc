#include "cpp.h"

#define err_if(e, ...) do {                 \
        if (unlikely((e))) {                \
            fputs("fatal error: ", stderr); \
            fprintf(stderr, __VA_ARGS__);   \
            fputc('\n', stderr);            \
            cpp_file_cleanup();             \
            exit(1);                        \
        }                                   \
    } while (0)

#define MAX_SYSDIR     128

static cpp_file g_files[CPP_FILE_MAX_USED];
static int g_file_count = 1; /* 0 is reserved */

static const char *g_sysdir[MAX_SYSDIR];
static int g_sysdir_count;

void cpp_file_setup(void)
{
    /* initialize system directory for #include <...> */
    g_sysdir[0] = "/usr/include";
    g_sysdir[1] = "/usr/local/include";
    g_sysdir[2] = "/usr/include/x86_64-linux-gnu";
    g_sysdir_count = 3;
}

void cpp_file_cleanup(void)
{
    int i;

    for (i = 3; i < g_sysdir_count; i++)
        free((char *)g_sysdir[i]);

    g_sysdir_count = 0;

    for (i = 1; i < g_file_count; i++) {
        cpp_file *f = &g_files[i];
        if (!HAS_FLAG(f->flags, CPP_FILE_FREED)) {
            free((char *)f->dirpath);
            free((char *)f->path);
            free((char *)f->name);
            free(f->data);
        }
    }
}

void cpp_file_add_sysdir(const char *_name)
{
    char *name;
    err_if(g_sysdir_count >= MAX_SYSDIR, "too many directories");
    name = strdup(_name);
    err_if(name == NULL, "out of memory while adding include search path");
    g_sysdir[g_sysdir_count++] = (const char *)name;
}

/* if #include <...>, `cwd` is NULL. */
cpp_file *cpp_file_open(const char *path, const char *cwd)
{
    struct stat sb;
    char buf[PATH_MAX + 1];
    unsigned int pathlen, filesize;

    if (g_file_count == CPP_FILE_MAX_USED) {
        errno = ENFILE;
        return NULL;
    }

    pathlen = strlen(path);

    if (path[0] != '/') {
        if (cwd != NULL) {
            snprintf(buf, PATH_MAX, "%s/%s", cwd, path);
            if (stat(buf, &sb) == -1) {
                if (errno != ENOENT)
                    return NULL;
                // else fallthrough and try to #include <...>
            } else {
                goto open_file;
            }
        }
        int i;
        for (i = 0; i < g_sysdir_count; i++) {
            snprintf(buf, PATH_MAX, "%s/%s", g_sysdir[i], path);
            if (stat(buf, &sb) == -1) {
                if (errno != ENOENT)
                    return NULL;
            } else {
                goto open_file;
            }
        }
        // ENOENT
        return NULL;
    } else {
        pathlen = MIN(pathlen, PATH_MAX);
        memcpy(buf, path, pathlen);
        buf[pathlen] = 0;
        if (stat(buf, &sb) == -1)
            return NULL; // absolute path is special
    }

open_file:

    filesize = sb.st_size;
    if (filesize > CPP_FILE_MAX_SIZE) {
        errno = EFBIG;
        return NULL;
    } else if (!S_ISREG(sb.st_mode)) {
        // Unfortunately, there's no errno for "not a regular file"
        errno = S_ISDIR(sb.st_mode) ? EISDIR : EINVAL;
        return NULL;
    }

    int fd = open(buf, O_RDONLY);
    if (fd == -1) // ENOENT is impossible here
        return NULL;

    unsigned int allocsize = ALIGN(filesize + 4, 8); // 4 bytes padding
    unsigned char *data = malloc(allocsize);
    if (data == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    // We are going to read() in block, so tell the kernel that the pattern
    // used for read()-ing will be sequential.
    // If error, it's ignored but errno must be saved somewhere.
    int saved_errno = errno;
    posix_fadvise(fd, 0, sb.st_size, POSIX_FADV_SEQUENTIAL);
    errno = saved_errno;

    unsigned char flags = 0;
    ssize_t byte_read = 0, offset = 0;
    ssize_t byte_max = (ssize_t)filesize; // ...

    while (1) {
        byte_read = read(fd, data + offset, MIN(8192, byte_max - offset));
        if (byte_read <= 0)
            break;
        offset += byte_read;
    }

    close(fd);

    if (offset > 0 && data[offset - 1] != '\n') {
        flags |= CPP_FILE_NONL; // For diagnostic
        data[offset] = '\n';
        data[offset + 1] = 0;
    } else {
        data[offset] = 0;
    }

    cpp_file *file = &g_files[g_file_count];
    file->id = g_file_count++;
    file->flags = flags;
    file->size = filesize;
    file->data = data;
    file->name = (const char *)strdup(path);
    file->path = (const char *)strdup(buf);
    char *p = strrchr(buf, '/'); *p = '\0';
    file->dirpath = (const char *)strdup(buf);

    errno = 0;
    return file;
}

void cpp_file_close(cpp_file *file)
{
    free((char *)file->dirpath); file->dirpath = NULL;
    free((char *)file->path); file->path = NULL;
    free((char *)file->name); file->name = NULL;
    free(file->data); file->data = NULL;
    file->flags |= CPP_FILE_FREED;
}

cpp_file *cpp_file_id(ushort id)
{
    if (id != 0 && id < g_file_count)
        return &g_files[id];
    errno = EINVAL;
    return NULL;
}
