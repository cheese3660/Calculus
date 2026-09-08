#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <unistd.h>
#include <fcntl.h>

#include "common/archive.h"
#include "common/debug.h"

typedef int (*callback_t)(void *cookie, const char *rel_path, const char *root_path, const char *full_path);

static int strcmpsort(const struct dirent **a, const struct dirent **b)
{
    return strcmp((*a)->d_name, (*b)->d_name);
}

static int magicfilter(const struct dirent *value)
{
    if (strcmp(value->d_name, ".") == 0)
        return 0;
    if (strcmp(value->d_name, "..") == 0)
        return 0;
    return 1;
}

// Helper for iterating over a directory in a stable manner used for a depth first search
static int iterate(const char *dir, const char *root_dir, callback_t callback, void *cookie)
{
    int retval = 0;

    size_t dir_len = strlen(dir);
    size_t root_dir_len = strlen(root_dir);
    size_t fullname_len = dir_len + root_dir_len + 1 /* '/' */;
    char *fullname = malloc(fullname_len + 1);
    memcpy(fullname, root_dir, root_dir_len);
    fullname[root_dir_len] = '/';
    memcpy(fullname + root_dir_len + 1, dir, dir_len);
    fullname[fullname_len] = 0;

    struct dirent **entries;
    int n_dirs = scandir(fullname, &entries, magicfilter, strcmpsort);
    if (n_dirs == -1)
    {
        perror("scandir");
        free(fullname);
        return -1;
    }

    for (int i = 0; i < n_dirs; i++)
    {
        struct dirent *entry = entries[i];

        // If we are instead exiting out, we want to finish cleaning up before we terminate
        if (retval != 0)
        {
            free(entry);
            continue;
        }

        size_t entry_len = strlen(entry->d_name);
        size_t entry_rel_len = dir_len + 1 /* '/' */ + entry_len;
        char *entry_relpath = malloc(entry_rel_len + 1);
        size_t entry_full_len = fullname_len + 1 /* '/' */ + entry_len;
        char *entry_fullpath = malloc(entry_full_len + 1);
        memcpy(entry_relpath, dir, dir_len);
        entry_relpath[dir_len] = '/';
        memcpy(entry_fullpath, fullname, fullname_len);
        entry_fullpath[fullname_len] = '/';

        memcpy(entry_relpath + dir_len + 1, entry->d_name, entry_len);
        entry_relpath[entry_rel_len] = 0;
        memcpy(entry_fullpath + fullname_len + 1, entry->d_name, entry_len);
        entry_fullpath[entry_full_len] = 0;

        // This is what determines that we are exiting out
        retval = callback(cookie, entry_relpath, root_dir, entry_fullpath);

        free(entry_fullpath);
        free(entry_relpath);
        free(entry);
    }

    free(entries);
    free(fullname);
    return retval;
}

typedef enum
{
    ATTR_EXECUTE = 0x01,
    ATTR_WRITE = 0x02,
    ATTR_READ = 0x04,
    ATTR_RES1 = 0x08,
    ATTR_RES2 = 0x10,
    ATTR_FILE = 0x20,
    ATTR_DIR = 0x40,
    ATTR_LINK = 0x80
} attr_flags_t;

inline static uint8_t get_attrs(uint32_t mode)
{
    uint8_t ret = 0;
    if (S_ISLNK(mode))
    {
        ret |= ATTR_LINK;
    }
    else if (S_ISDIR(mode))
    {
        ret |= ATTR_DIR;
    }
    else if (S_ISREG(mode))
    {
        ret |= ATTR_FILE;
    }
    else
    {
        return 0xff; // This is a failure signal
    }
    // Only add permissions to nonlinks
    if (!S_ISLNK(mode))
        ret |= ((mode & S_IRWXU) >> 6) & 0b111;
    return ret;
}

static int hash_single(void *cookie, const char *rel_path, const char *root_path, const char *full_path)
{
    sha256_ingest_t *ingest = cookie;
    debug("Hashing a single file, relpath = `%s` (full `%s`)", rel_path, full_path);

    struct stat stat_result;

    // Use lstat here because this could be a symlink
    int s = lstat(full_path, &stat_result);
    if (s != 0)
    {
        perror("lstat");
        return s;
    }

    uint32_t mode = stat_result.st_mode;

    uint8_t attr = get_attrs(mode);

    if (attr == 0xff)
        return 0;

    // Now we append this data to the hash as it would be in the actual file
    sha256_append(ingest, rel_path, strlen(rel_path) + 1 /* Include the null byte */);

    sha256_append(ingest, &attr, 1);

    if (S_ISLNK(mode))
    {
        static char linkbuf[PATH_MAX];
        ssize_t len = readlink(full_path, linkbuf, PATH_MAX);
        if (len == -1)
        {
            // We have an invalid paramter here
            perror("readlink");
            return -1;
        }
        linkbuf[len] = 0;
        sha256_append(ingest, linkbuf, len + 1 /* account for the null here */);
    }

    if (S_ISREG(mode))
    {
        uint64_t size = stat_result.st_size;
        sha256_append(ingest, (uint8_t *)&size, 8);

        FILE *res = fopen(full_path, "r");
        if (res == nullptr)
        {
            perror("fopen");
            return -1;
        }
        sha256_appendf(ingest, res);
        fclose(res);
    }

    if (S_ISDIR(mode))
    {
        // We recursively search here now
        int res = iterate(rel_path, root_path, hash_single, cookie);
        return res;
    }
    // Impossible but
    return 0;
}

int car_hash(const char *path, sha256_t *result)
{
    if (path == nullptr)
        panic("path must not be null");

    if (result == nullptr)
        panic("result must not be null");

    static char real[PATH_MAX];
    char *real_res = realpath(path, real);
    if (real_res == nullptr)
    {
        perror("realpath");
        return -1;
    }

    sha256_ingest_t ingest = {};
    // We already know that we are going to have the CAR header
    sha256_append(&ingest, "CAR", 4);

    int res = iterate(".", real_res, hash_single, &ingest);
    if (res != 0)
    {
        return res;
    }

    // And then at the end a null entry to finalize the archive
    sha256_append(&ingest, "", 1);
    *result = sha256_finalize(&ingest);
    return 0;
}

static int archive_single(void *cookie, const char *rel_path, const char *root_path, const char *full_path)
{
    // The cookie is the file we are writing into
    FILE *file = cookie;

    struct stat stat_result;

    // Use lstat here because this could be a symlink
    int s = lstat(full_path, &stat_result);
    if (s != 0)
    {
        perror("lstat");
        return s;
    }

    uint32_t mode = stat_result.st_mode;

    uint8_t attr = get_attrs(mode);

    if (attr == 0xff)
        return 0;

    // Okay now this is where we diverge - we start with the name here
    fwrite(rel_path, 1, strlen(rel_path) + 1, file);
    fwrite(&attr, 1, 1, file);

    if (S_ISLNK(mode))
    {
        static char linkbuf[PATH_MAX];
        ssize_t len = readlink(full_path, linkbuf, PATH_MAX);
        if (len == -1)
        {
            // We have an invalid paramter here
            perror("readlink");
            return -1;
        }
        linkbuf[len] = 0;
        size_t res = fwrite(linkbuf, 1, len + 1, file);
        if (res != (size_t)len + 1)
            perror("fwrite");
        return res;
    }

    if (S_ISREG(mode))
    {
        uint64_t size = stat_result.st_size;
        // res is the number of elements written
        size_t res = fwrite(&size, sizeof(uint64_t), 1, file);
        if (res != 1)
        {
            perror("fwrite");
            return -1;
        }

        FILE *other = fopen(full_path, "r");
        if (other == nullptr)
        {
            perror("fopen");
            return -1;
        }

        static uint8_t buffer[4096]; // Use a 4 kilobyte r/w buffer

        while (!feof(other))
        {
            size_t read = fread(buffer, 1, 4096, other);
            if (read != 4096 && ferror(other))
            {
                perror("fread");
                fclose(other);
                return -1;
            }
            size_t written = fwrite(buffer, 1, read, file);
            if (written != read)
            {
                perror("fwrite");
                fclose(other);
                return -1;
            }
        }

        fclose(other);
    }

    if (S_ISDIR(mode))
    {
        // We recursively search here now
        int res = iterate(rel_path, root_path, archive_single, cookie);
        return res;
    }

    return 0;
}

static int archive_single_ro(void *cookie, const char *rel_path, const char *root_path, const char *full_path)
{
    // The cookie is the file we are writing into
    FILE *file = cookie;

    struct stat stat_result;

    // Use lstat here because this could be a symlink
    int s = lstat(full_path, &stat_result);
    if (s != 0)
    {
        perror("lstat");
        return s;
    }

    uint32_t mode = stat_result.st_mode;

    uint8_t attr = get_attrs(mode);

    if (attr == 0xff)
        return 0;

    // Disable the write bits
    attr &= ~2;

    // Okay now this is where we diverge - we start with the name here
    fwrite(rel_path, 1, strlen(rel_path) + 1, file);
    fwrite(&attr, 1, 1, file);

    if (S_ISLNK(mode))
    {
        static char linkbuf[PATH_MAX];
        ssize_t len = readlink(full_path, linkbuf, PATH_MAX);
        if (len == -1)
        {
            // We have an invalid paramter here
            perror("readlink");
            return -1;
        }
        linkbuf[len] = 0;
        size_t res = fwrite(linkbuf, 1, len + 1, file);
        if (res != (size_t)len + 1)
            perror("fwrite");
        return res;
    }

    if (S_ISREG(mode))
    {
        uint64_t size = stat_result.st_size;
        // res is the number of elements written
        size_t res = fwrite(&size, sizeof(uint64_t), 1, file);
        if (res != 1)
        {
            perror("fwrite");
            return -1;
        }

        FILE *other = fopen(full_path, "r");
        if (other == nullptr)
        {
            perror("fopen");
            return -1;
        }

        static uint8_t buffer[4096]; // Use a 4 kilobyte r/w buffer

        while (!feof(other))
        {
            size_t read = fread(buffer, 1, 4096, other);
            if (read != 4096 && ferror(other))
            {
                perror("fread");
                fclose(other);
                return -1;
            }
            size_t written = fwrite(buffer, 1, read, file);
            if (written != read)
            {
                perror("fwrite");
                fclose(other);
                return -1;
            }
        }

        fclose(other);
    }

    if (S_ISDIR(mode))
    {
        // We recursively search here now
        int res = iterate(rel_path, root_path, archive_single_ro, cookie);
        return res;
    }

    return 0;
}

int car_archive(const char *path, FILE *target)
{
    if (path == nullptr)
        panic("path must not be null");
    if (target == nullptr)
        panic("target must not be null");

    // Get the real path
    static char real[PATH_MAX];
    char *real_res = realpath(path, real);
    if (real_res == nullptr)
    {
        perror("realpath");
        return -1;
    }

    // Write the header
    size_t written = fwrite("CAR", 1, 4, target);
    if (written != 4)
    {
        perror("fwrite");
        return -1;
    }

    // Now do the recursive writing
    int res = iterate(".", real_res, archive_single, target);
    if (res != 0)
    {
        return res;
    }

    // And write the last null byte to signal the end of the archive
    written = fwrite("", 1, 1, target);
    if (written != 1)
    {
        perror("fwrite");
        return -1;
    }
    return 0;
}

int car_archive_ro(const char *path, FILE *target)
{
    if (path == nullptr)
        panic("path must not be null");
    if (target == nullptr)
        panic("target must not be null");

    // Get the real path
    static char real[PATH_MAX];
    char *real_res = realpath(path, real);
    if (real_res == nullptr)
    {
        perror("realpath");
        return -1;
    }

    // Write the header
    size_t written = fwrite("CAR", 1, 4, target);
    if (written != 4)
    {
        perror("fwrite");
        return -1;
    }

    // Now do the recursive writing
    int res = iterate(".", real_res, archive_single_ro, target);
    if (res != 0)
    {
        return res;
    }

    // And write the last null byte to signal the end of the archive
    written = fwrite("", 1, 1, target);
    if (written != 1)
    {
        perror("fwrite");
        return -1;
    }
    return 0;
}

typedef enum
{
    HEADER = 0,  // Reading the "CAR\0" header
    ENT_NAME,    // Reading an entry header
    ENT_MODE,    // Reading an entry mode
    LINK_TARGET, // Reading a link target
    FILE_LENGTH, // Reading file length
    FILE_DATA,   // Reading file data
    STOPPED,     // Stopped for whatever reason
} extract_state_t;

// We use static variables here because this is not meant to be reentrant, we can make this into a structure later
static extract_state_t extract_state;

static char header[4];
static size_t header_read_count; // The amount of data read from the header

static char ent_path[PATH_MAX];
static size_t ent_root_len;
static size_t ent_read_count;
static uint8_t ent_attr;

static size_t file_length_read;
static uint64_t file_bytes_remaining;
static int extract_fd; // the current file being written to

static char link_target[PATH_MAX];
static size_t link_target_count;

typedef enum
{
    EXTRACT_CONTINUE,
    EXTRACT_ERROR,
    EXTRACT_END,
} extract_result_t;

static void extract_reset(const char *root)
{
    ent_root_len = strlen(root);
    memcpy(ent_path, root, ent_root_len);
    ent_path[ent_root_len] = '/';
    extract_state = HEADER;
    header_read_count = 0;
}

static extract_result_t process_extract(const char *data, size_t n)
{
    while (n > 0)
    {
        switch (extract_state)
        {
        case HEADER:
            while (header_read_count < 4)
            {
                if (n == 0)
                {
                    return EXTRACT_CONTINUE;
                }
                header[header_read_count++] = *data;
                data++;
                n--;
            }

            if (strncmp("CAR", header, 4) != 0)
            {
                fprintf(stderr, "error: invalid car header\n");
                extract_state = STOPPED;
                return EXTRACT_ERROR;
            }
            ent_read_count = 0;
            extract_state = ENT_NAME;
            [[fallthrough]];
        case ENT_NAME:
            // This can likely be sped up into some form of memcpy
            while (n != 0)
            {
                // We are reading into the postfix part of the path
                // so this is x/./x is how it will always end up
                ent_path[ent_root_len + 1 + ent_read_count++] = *data;
                data++, n--;
                if (ent_path[ent_root_len + ent_read_count] == 0)
                    break;

                if (ent_read_count + ent_root_len + 1 >= PATH_MAX)
                {
                    fprintf(stderr, "error: entry name too long\n");
                    extract_state = STOPPED;
                    return EXTRACT_ERROR;
                }
            }

            if (ent_path[ent_root_len + ent_read_count] != 0)
            {
                // Break here instead
                break;
            }

            if (ent_read_count == 1 /* Has to be 1 null byte*/)
            {
                // In this case this is the terminating entry
                extract_state = STOPPED;
                return EXTRACT_END;
            }
            extract_state = ENT_MODE;
            [[fallthrough]];
        case ENT_MODE:
            // need this because of the fallthrough
            if (n == 0)
                return EXTRACT_CONTINUE;
            ent_attr = (uint8_t)*(data++);
            // Splat out the permissions
            mode_t permissions = (ent_attr & 0b111);
            permissions |= (permissions << 3) | (permissions << 6);
            if ((ent_attr & ATTR_DIR) != 0)
            {
                // Directory
                debug("Creating directory '%s'", ent_path);
                int status = mkdir(ent_path, permissions);
                if (status == -1)
                {
                    perror("mkdir");
                    extract_state = STOPPED;
                    return EXTRACT_ERROR;
                }
                ent_read_count = 0;
                extract_state = ENT_NAME;
                break;
            }

            if ((ent_attr & ATTR_LINK) != 0)
            {
                debug("Creating symlink '%s'", ent_path);
                extract_state = LINK_TARGET;
                link_target_count = 0;
                break;
            }

            if ((ent_attr & ATTR_FILE) == 0)
            {
                fprintf(stderr, "error: invalid attribute bit\n");
                extract_state = STOPPED;
                return EXTRACT_ERROR;
            }

            debug("Creating file '%s'", ent_path);
            extract_fd = open(ent_path, O_WRONLY | O_CREAT | O_EXCL, permissions);
            if (extract_fd == -1)
            {
                perror("open");
                extract_state = STOPPED;
                return EXTRACT_ERROR;
            }
            file_length_read = 0;
            [[fallthrough]];
        case FILE_LENGTH:
            while (file_length_read < 8)
            {
                if (n == 0)
                {
                    return EXTRACT_CONTINUE;
                }

                ((char *)&file_bytes_remaining)[file_length_read++] = *(data++);
                n--;
            }
            debug("File length %ld", file_bytes_remaining);
            [[fallthrough]];
        case FILE_DATA:
            // Again due to fallthrough we check again
            if (n == 0)
            {
                return EXTRACT_CONTINUE;
            }
            size_t to_read = n > file_bytes_remaining ? file_bytes_remaining : n;

            ssize_t written = write(extract_fd, data, to_read);
            if (written == -1)
            {
                perror("write");
                close(extract_fd);
                extract_state = STOPPED;
                return EXTRACT_ERROR;
            }

            file_bytes_remaining -= written;
            data += written;
            n -= written;

            if (file_bytes_remaining == 0)
            {
                close(extract_fd);
                ent_read_count = 0;
                extract_state = ENT_NAME;
            }
            break;
        case LINK_TARGET:
            while (n != 0)
            {
                link_target[link_target_count++] = *(data++);
                n--;
                if (link_target[link_target_count - 1] == 0)
                {
                    break;
                }

                if (link_target_count == PATH_MAX)
                {

                    fprintf(stderr, "error: link name too long\n");
                    extract_state = STOPPED;
                    return EXTRACT_ERROR;
                }
            }

            if (link_target[link_target_count - 1] != 0)
            {
                // This usually means N = 0;
                break;
            }
            int link_res = symlink(ent_path, link_target);
            if (link_res == -1)
            {
                perror("symlink");
                extract_state = STOPPED;
                return EXTRACT_ERROR;
            }
            ent_read_count = 0;
            extract_state = ENT_NAME;
            break;
        case STOPPED:
            return EXTRACT_END;
        }
    }
    return EXTRACT_CONTINUE;
}

static void extract_cleanup()
{
    // We want to close any file descriptor we may have opened here
    close(extract_fd);
}

int car_extract(FILE *car_file, const char *path)
{
    if (car_file == nullptr)
        panic("car_file must not be null");
    if (path == nullptr)
        panic("path must not be null");

    // Get the real path
    static char real[PATH_MAX];
    char *real_res = realpath(path, real);
    if (real_res == nullptr)
    {
        perror("realpath");
        return -1;
    }

    struct stat stat_res;
    int res = stat(real_res, &stat_res);
    if (res == -1)
    {
        perror("stat");
        return -1;
    }

    if (!S_ISDIR(stat_res.st_mode))
        panic("path must be a directory");

    extract_reset(real_res);

    // Now we do the reading loop
    static char buffer[4096]; // 4 KB pages

    while (!feof(car_file))
    {
        size_t read = fread(buffer, 1, 4096, car_file);

        if (ferror(car_file))
        {
            perror("fread");
            extract_cleanup();
            return -1;
        }

        switch (process_extract(buffer, read))
        {
        case EXTRACT_CONTINUE:
            break;
        case EXTRACT_ERROR:
            goto failure;
        case EXTRACT_END:
            goto success;
        }
    }
    // Here is where we EOF
    fprintf(stderr, "error: car stream truncated\n");
failure:
    extract_cleanup();
    return -1;
success:
    extract_cleanup();
    return 0;
}