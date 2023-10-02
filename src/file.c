#include "cpp.h"

static cpp_file g_files[CPP_FILE_MAX_USED];
static int g_file_count = 1; /* 0 is reserved */

void cpp_file_setup(void)
{
    cpp_file *f = &g_files[0];
    f->name = LITREF("<command-line>");
    f->path = f->name;
    f->dirpath = LITREF(".");
}

void cpp_file_cleanup(void)
{
    int i;

    for (i = 1; i < g_file_count; i++) {
        cpp_file *f = &g_files[i];
        if (!HAS_FLAG(f->flags, CPP_FILE_FREED))
            free(f->data);
    }
}

cpp_file *cpp_file_open(const char *path, const char *name)
{
    return cpp_file_open2(string_ref_new(path), string_ref_new(name), NULL);
}

cpp_file *cpp_file_open2(string_ref _path, string_ref name, struct stat *sb)
{
    uchar flags;
    struct stat sb2;
    int fd, saved_errno;
    uint filesize, psize;
    ssize_t byte_read, byte_max, offset;
    const char *p, *path = string_ref_ptr(_path);

    if (g_file_count == CPP_FILE_MAX_USED) {
        errno = ENFILE;
        return NULL;
    }

    if (sb == NULL) {
        sb = &sb2;
        if (stat(path, sb) != 0)
            return NULL;
    }

    filesize = sb->st_size;
    if (filesize > CPP_FILE_MAX_SIZE) {
        errno = EFBIG;
        return NULL;
    } else if (!S_ISREG(sb->st_mode)) {
        errno = S_ISDIR(sb->st_mode) ? EISDIR : EINVAL;
        return NULL;
    }

    fd = open(path, O_RDONLY);
    if (fd == -1)
        return NULL;

    uint allocsize = ALIGN(filesize + 4, 8); /* 4 bytes padding */
    uchar *data = malloc(allocsize);
    if (data == NULL) {
        close(fd);
        errno = ENOMEM;
        return NULL;
    }

    /* We are going to read() in block, so tell the kernel that the pattern
     * used for read()-ing will be sequential.
     * If error, it's ignored but errno must be saved somewhere. */
    saved_errno = errno;
    posix_fadvise(fd, 0, sb->st_size, POSIX_FADV_SEQUENTIAL);
    errno = saved_errno;

    flags = 0;
    byte_read = 0, offset = 0;
    byte_max = (ssize_t)filesize;

    while (1) {
        byte_read = read(fd, data + offset, MIN(8192, byte_max - offset));
        if (byte_read <= 0)
            break;
        offset += byte_read;
    }

    close(fd);

    if (offset > 0 && data[offset - 1] != '\n') {
        flags |= CPP_FILE_NONL; /* For diagnostic */
        data[offset] = '\n';
        data[offset + 1] = 0;
    } else {
        data[offset] = 0;
    }

    cpp_file *file = &g_files[g_file_count];
    file->no = g_file_count++;
    file->flags = flags;
    file->size = filesize;
    file->inode = (uint)sb->st_ino;
    file->devid = (uint)sb->st_dev;
    file->data = data;
    file->name = name;
    file->path = _path;

    p = strrchr(path, '/');
    if (p != NULL) {
        psize = (unsigned int)(p - path);
        file->dirpath = string_ref_newlen(path, psize);
    } else {
        file->dirpath = LITREF(".");
    }

    errno = 0;
    return file;
}

void cpp_file_close(cpp_file *file)
{
    free(file->data); file->data = NULL;
    file->flags |= CPP_FILE_FREED;
}

cpp_file *cpp_file_no(ushort no)
{
    if (no != 0 && no < g_file_count)
        return &g_files[no];
    errno = EINVAL;
    return NULL;
}
