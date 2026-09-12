/******************************************************************************
 *
 *  build.c
 *  author: Lexi Allen
 *  license: MIT
 *  last updated: 9/12/2026
 *
 *  This file contains the implementation of the sandboxed builder system that
 *  is what takes a derivative recipe and creates its artifact in the store.
 *
 *****************************************************************************/

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include "thirdparty/stb_ds.h"

#include "calculus/build.h"
#include "calculus/paths.h"
#include "common/archive.h"
#include "common/debug.h"
#include "common/fs.h"
#include "common/string.h"

/******************************************************************************
 *
 * GENERAL BUILDING UTILITIES
 *
 * ensure_paths() - Ensure that all the necessary directories for building are
 *                  created
 * copy_archive() - Copy the data from one archive to another
 * extract_archive() - Extract an archive to a given directory, ensuring that
 *                     the archive is extracted readonly
 * copy_directory() - Copy 2 directories between eachother using libarchive
 *
 *****************************************************************************/

static void ensure_paths()
{
    static bool paths_ensured = false;
    if (paths_ensured)
        return;
    paths_ensured = true;

    if (fs_ensure_dir(CALCULUS_LOGS_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");

    if (fs_ensure_dir(CALCULUS_BUILD_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");

    if (fs_ensure_dir(CALCULUS_STORE_DIRECTORY) != 0)
        panic("could not ensure calculus directories!");
}

// Some very basic helper functions for extracting an archive using libarchive
static int copy_archive(struct archive *from, struct archive *to)
{
    int r;
    const void *buff;
    size_t size;
    la_int64_t offset;

    for (;;)
    {
        r = archive_read_data_block(from, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return ARCHIVE_OK;
        if (r != ARCHIVE_OK)
        {
            fprintf(stderr, "Error reading: %s\n", archive_error_string(from));
            return r;
        }
        r = archive_write_data_block(to, buff, size, offset);
        if (r != ARCHIVE_OK)
        {
            fprintf(stderr, "Error writing: %s\n", archive_error_string(to));
            return r;
        }
    }
}

// Extract as readonly
static int extract_archive(const char *file, const char *directory)
{
    int result;

    struct archive *from = archive_read_new();
    archive_read_support_filter_all(from);
    archive_read_support_format_all(from);

    struct archive *to = archive_write_disk_new();
    archive_write_disk_set_options(to, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(to);

    if ((result = archive_read_open_filename(from, file, 10240)) != ARCHIVE_OK)
    {
        fprintf(stderr, "Could not open source archive: %s\n", archive_error_string(from));
        return result;
    }

    struct archive_entry *entry;

    while (archive_read_next_header(from, &entry) == ARCHIVE_OK)
    {
        const char *current_path = archive_entry_pathname(entry);
        mode_t current_perms = archive_entry_perm(entry);
        string_t *temp = s_fmt_p(PATH_MAX * 2, "%s/%s", directory, current_path);
        archive_entry_set_pathname(entry, temp->cstring);

        const char *hardlink_target = archive_entry_hardlink(entry);
        if (hardlink_target != nullptr)
        {
            string_t *temp2 = s_fmt_p(PATH_MAX * 2, "%s/%s", directory, hardlink_target);
            archive_entry_set_hardlink(entry, temp2->cstring);
            s_free(temp2);
        }

        // Make the permissions readonly while extracting
        archive_entry_set_perm(entry, current_perms & ~0222);
        if ((result = archive_write_header(to, entry)))
        {
            fprintf(stderr, "Warning reading entry %s: %s\n", temp->cstring, archive_error_string(to));
        }
        else if (archive_entry_size(entry) > 0 && (result = copy_archive(from, to)))
        {
            fprintf(stderr, "Error writing entry %s\n", temp->cstring);
        }

        s_free(temp);

        archive_write_finish_entry(to);
    }

    archive_read_close(from);
    archive_read_free(from);
    archive_write_close(to);
    archive_write_free(to);

    return result;
}

static int copy_directory(const char *from_directory, const char *directory)
{
    int result;

    size_t from_len = strlen(from_directory);

    struct archive *from = archive_read_disk_new();

    struct archive *to = archive_write_disk_new();
    archive_write_disk_set_options(to, ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT);
    archive_write_disk_set_standard_lookup(to);

    if ((result = archive_read_disk_open(from, from_directory)) != ARCHIVE_OK)
    {
        fprintf(stderr, "Could not open source archive: %s\n", archive_error_string(from));
        return result;
    }

    struct archive_entry *entry;

    while (archive_read_next_header(from, &entry) == ARCHIVE_OK)
    {
        archive_read_disk_descend(from);

        const char *current_path = archive_entry_pathname(entry);

        if (strncmp(current_path, from_directory, from_len) == 0)
        {
            current_path += from_len;
            while (*current_path == '/')
            {
                current_path++;
            }
        }

        mode_t current_perms = archive_entry_perm(entry);
        // snprintf(archive_fullpath, sizeof(archive_fullpath), "%s/%s", directory, current_path);
        string_t *temp = s_fmt_p(PATH_MAX * 2, "%s/%s", directory, current_path);
        archive_entry_set_pathname(entry, temp->cstring);

        const char *hardlink_target = archive_entry_hardlink(entry);
        if (hardlink_target != nullptr)
        {
            // Fix extraction code
            if (strncmp(hardlink_target, from_directory, from_len) == 0)
            {
                hardlink_target += from_len;
                while (*hardlink_target == '/')
                {
                    hardlink_target++;
                }
            }

            string_t *temp2 = s_fmt_p(PATH_MAX * 2, "%s/%s", directory, hardlink_target);
            archive_entry_set_hardlink(entry, temp2->cstring);
            s_free(temp2);
        }

        // Make the permissions readonly while extracting
        archive_entry_set_perm(entry, current_perms & ~0222);
        if ((result = archive_write_header(to, entry)))
        {
            fprintf(stderr, "Warning reading entry %s: %s\n", temp->cstring, archive_error_string(to));
        }
        else if (archive_entry_size(entry) > 0 && (result = copy_archive(from, to)))
        {
            fprintf(stderr, "Error writing entry %s\n", temp->cstring);
        }

        s_free(temp);

        archive_write_finish_entry(to);
    }

    archive_read_close(from);
    archive_read_free(from);
    archive_write_close(to);
    archive_write_free(to);

    return result;
}

/******************************************************************************
 *
 * TARBALL HANDLER
 *
 * build_tarball() - fetches a tarball from the url, verifies it's hash, and
 *                   optionally extracts it
 *
 *****************************************************************************/

static int build_tarball(fetch_tarball_derivative_t *tarball)
{
    // static char fp_buffer[512 /* We can assume a lot less of a size here because there is a max size on paths*/];
    string_t *filepath = nullptr;

    int result = -1;
    CURL *curl = nullptr;
    FILE *fp = nullptr;
    char *outfile_name = nullptr;
    curl = curl_easy_init();

    if (!curl)
    {
        fprintf(stderr, "initializing curl failed for tarball\n");
        goto done;
    }

    // snprintf(fp_buffer, 512, "%s/%s", CALCULUS_STORE_DIRECTORY, get_derivative_node_name((derivative_header_t *)tarball));
    filepath = s_fmt_p(512, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name((derivative_header_t *)tarball));

    if (!tarball->extract)
    {
        outfile_name = filepath->cstring;
    }
    else
    {
        outfile_name = "/tmp/downloaded_tarball";
    }

    fp = fopen(outfile_name, "w");

    if (!fp)
    {
        fprintf(stderr, "error opening file to download tarball %s to: %s\n", tarball->url, strerror(errno));
        goto done;
    }

    curl_easy_setopt(curl, CURLOPT_URL, tarball->url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode error = curl_easy_perform(curl);
    if (error != CURLE_OK)
    {
        fprintf(stderr, "error downloading tarball %s via curl: %s\n", tarball->url, curl_easy_strerror(error));
        goto done;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400)
    {
        fprintf(stderr, "downloading tarball %s failed with code %ld\n", tarball->url, http_code);
        remove(outfile_name);
        goto done;
    }

    fclose(fp);
    fp = fopen(outfile_name, "r");
    if (!fp)
    {
        fprintf(stderr, "error opening tarball %s after downloading: %s\n", tarball->url, strerror(errno));
        goto done;
    }

    sha256_t file_hash = sha256_hashf(fp);
    fclose(fp);
    fp = nullptr;

    if (sha256_cmp(&file_hash, &tarball->dheader.dhash) != 0)
    {
        fprintf(stderr, "tarball %s hash is not what was expected!\n", tarball->url);
        fprintf(stderr, "expected: %s\n", sha256_to_hex(&tarball->dheader.dhash));
        fprintf(stderr, "got: %s\n", sha256_to_hex(&file_hash));
        remove(outfile_name);
        goto done;
    }

    if (!tarball->extract)
    {
        // Mark as read only
        chmod(outfile_name, 0444);
        // And owned by root
        if (chown(outfile_name, 0, 0) == -1)
        {
            fprintf(stderr, "error changing owner of tarball %s file: %s\n", tarball->url, strerror(errno));
            remove(outfile_name);
            goto done;
        }
        result = 0;
        goto done;
    }

    // Now let's make our directory
    if (mkdir(filepath->cstring, 0777) == -1)
    {
        fprintf(stderr, "error creating output directory for built tarball %s: %s\n", tarball->url, strerror(errno));
        remove(outfile_name);
        goto extract_done;
    }

    if (extract_archive(outfile_name, filepath->cstring) != ARCHIVE_OK)
    {
        fprintf(stderr, "error extracting tarball %s\n", tarball->url);
        remove(outfile_name);
        goto extract_done;
    }

    // Mark as read only
    if (chmod(filepath->cstring, 0555) == -1)
    {
        fprintf(stderr, "error changing mod of output directory: %s\n", strerror(errno));
        goto extract_done;
    }

    // And owned by root
    if (chown(filepath->cstring, 0, 0) == -1)
    {
        fprintf(stderr, "error changing owner of output directory: %s\n", strerror(errno));
        goto extract_done;
    }

    result = 0;
extract_done:
    remove(outfile_name);
done:
    if (filepath)
        s_free(filepath);
    if (fp)
        fclose(fp);
    if (curl)
        curl_easy_cleanup(curl);
    return result;
}

/******************************************************************************
 *
 * STANDARD BUILD HANDLER
 *
 * setup_uid_map() - Handles setting up the UID map in an unshare environment
 * checked_mount() - Runs mount() and if it fails prints a message and exits
 * checked_mkdir() - Runs mkdir() and if it fails prints a message and exits
 * mkd_mount() - checked_mkdir() on mount target then checked_mount()
 * checked_touch() - Touches a file and if it fails prints a message and exits
 * mkf_mount() - checked_touch() on mount target then checked_mount()
 * mount_dependencies() - Mount all the dependencies of a derivative and
 *                        collect them in an environment variable alongside
 *                        the eventual sh & env symlinks
 * enter_jail() - Enters the unshare environment of the build process, after
 *                stdin and stdout are set up
 * copy_file() - Copy one file stream to another
 * monitor_jail() - Monitors the jail process and prints diagnostics
 * build_standard() - Build a standard derivative using a sandboxed jail
 * 
 *****************************************************************************/

static void setup_uid_map(uid_t uid, gid_t gid)
{
    int fd = open("/proc/self/setgroups", O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error setting up uid map (setgroups): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (write(fd, "deny", 4) == -1)
    {
        fprintf(stderr, "Error writing to uid map (setgroups): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    close(fd);

    fd = open("/proc/self/uid_map", O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error setting up uid map (uid_map): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    string_t *s = s_fmt_p(32, "0 %d 1\n", uid);
    if (write(fd, s->cstring, s->length) == -1)
    {
        fprintf(stderr, "Error writing to uid map (uid_map): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    close(fd);

    fd = open("/proc/self/gid_map", O_WRONLY);
    if (fd == -1)
    {
        fprintf(stderr, "Error setting up uid map (gid_map): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    s_setfn(s, 32, "0 %d 1\n", gid);
    if (write(fd, s->cstring, s->length) == -1)
    {
        fprintf(stderr, "Error writing to uid map (gid_map): %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    s_free(s);
    close(fd);
}

static void checked_mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data)
{
    if (mount(source, target, filesystemtype, mountflags, data) == -1)
    {
        fprintf(stderr, "Error mounting '%s' -> '%s': %s\n", source, target, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void checked_mkdir(const char *target, mode_t permissions)
{
    if (mkdir(target, permissions) == -1)
    {
        fprintf(stderr, "Error creating '%s': %s\n", target, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

static void mkd_mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data)
{
    checked_mkdir(target, 0755);
    checked_mount(source, target, filesystemtype, mountflags, data);
}

static void checked_touch(const char *target, mode_t permissions)
{
    int fd = open(target, O_WRONLY | O_CREAT | O_CLOEXEC, permissions);
    if (fd == -1)
    {
        fprintf(stderr, "Error creating '%s': %s\n", target, strerror(errno));
        exit(EXIT_FAILURE);
    }
    close(fd);
}

static void mkf_mount(const char *source, const char *target, const char *filesystemtype, unsigned long mountflags, const void *data)
{
    checked_touch(target, 0666);
    checked_mount(source, target, filesystemtype, mountflags, data);
}

static void mount_dependencies(standard_derivative_t *derivative, string_t *build_directory, string_t *bin_sh, string_t *bin_env, string_t *env_var)
{
    // First we create the directory to mount our dependencies into
    string_t *filename = s_fmt_p(PATH_MAX, "%s" CALCULUS_TRUE_PREFIX, build_directory->cstring);
    checked_mkdir(filename->cstring, 0755);

    s_setfn(filename, PATH_MAX, "%s" CHROOT_STORE_DIRECTORY, build_directory->cstring);
    checked_mkdir(filename->cstring, 0755);

    s_cat(filename, "/");

    // Then we go over every dependency
    for (size_t i = 0; i < derivative->num_dependencies; i++)
    {
        derivative_header_t* dependency = derivative->dependencies[i];
        string_t *target = s_cat_p(filename, get_derivative_node_name(dependency));
        string_t *local = s_fmt_p(PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name(dependency));
        string_t *temp = s_new_p(PATH_MAX);

        // First mounting them
        if (fs_isreg(local->cstring))
        {
            // We just mount this normally
            mkf_mount(local->cstring, target->cstring, nullptr, MS_BIND | MS_RDONLY, nullptr);
            goto cont;
        }

        if (!fs_isdir(local->cstring))
        {
            fprintf(stderr, "dependency %s is not a file or a directory\n", local->cstring);
            exit(EXIT_FAILURE);
        }

        // Then checking for our /bin/sh or /bin/bash, and /bin/env files that we need to symlink eventually
        if (bin_sh->length != 0)
            goto check_env;

        s_setfn(temp, PATH_MAX, "%s/bin/bash", local->cstring);
        if (fs_exists(temp->cstring))
        {
            s_setfn(bin_sh, PATH_MAX, "%s/bin/bash", target->cstring);
            goto check_env;
        }

        s_setfn(temp, PATH_MAX, "%s/bin/sh", local->cstring);
        if (fs_exists(temp->cstring))
            s_setfn(bin_sh, PATH_MAX, "%s/bin/sh", target->cstring);

    check_env:
        if (bin_env->length != 0)
            goto cont;

        s_setfn(temp, PATH_MAX, "%s/bin/env", local->cstring);
        if (fs_exists(temp->cstring))
            s_setfn(bin_env, PATH_MAX, "%s/bin/env", target->cstring);

    cont:
        if (env_var->length > 0)
            s_cat(env_var, ":");
        s_cats(env_var, target);

        s_free(temp);
        s_free(target);
        s_free(local);
    }
}

static void enter_jail(standard_derivative_t *derivative, string_t *build_directory)
{
    uid_t uid = getuid();
    gid_t gid = getgid();
    if (unshare(
            // Let us
            CLONE_NEWCGROUP |
            CLONE_NEWIPC |
            CLONE_NEWPID |
            CLONE_NEWUTS |
            CLONE_NEWNET |
            CLONE_NEWUSER |
            CLONE_NEWNS) == -1)
    {
        fprintf(stderr, "Error setting up unshare for %s: %s\n", derivative->name, strerror(errno));
        exit(EXIT_FAILURE);
    }
    setup_uid_map(uid, gid);

    pid_t builder = fork();
    if (builder < 0)
    {
        fprintf(stderr, "Error forking builder process for %s: %s", derivative->name, strerror(errno));
        exit(EXIT_FAILURE);
    }
    else if (builder == 0)
    {
        // Make sure we can't accidentally ruin mounts outside of this unshare
        checked_mount("none", "/", NULL, MS_REC | MS_PRIVATE, nullptr);

        // And mount our target root to itself for the pivot_root syscall
        checked_mount(build_directory->cstring, build_directory->cstring, nullptr, MS_BIND | MS_REC, nullptr);

        // Set up basic linux virtual filesystems

        // /proc
        string_t *filename = s_fmt_p(PATH_MAX, "%s/proc", build_directory->cstring);
        mkd_mount("proc", filename->cstring, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr);

        // /sys
        s_setfn(filename, PATH_MAX, "%s/sys", build_directory->cstring);
        mkd_mount("sysfs", filename->cstring, "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr);

        // /tmp
        s_setfn(filename, PATH_MAX, "%s/tmp", build_directory->cstring);
        mkd_mount("tmpfs", filename->cstring, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr);

        // /dev
        s_setfn(filename, PATH_MAX, "%s/dev", build_directory->cstring);
        mkd_mount("tmpfs", filename->cstring, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr);

        // Then some safe devices

        // /dev/null
        s_setfn(filename, PATH_MAX, "%s/dev/null", build_directory->cstring);
        mkf_mount("/dev/null", filename->cstring, nullptr, MS_BIND, nullptr);

        // /dev/zero
        s_setfn(filename, PATH_MAX, "%s/dev/zero", build_directory->cstring);
        mkf_mount("/dev/zero", filename->cstring, nullptr, MS_BIND, nullptr);

        // /dev/random
        s_setfn(filename, PATH_MAX, "%s/dev/random", build_directory->cstring);
        mkf_mount("/dev/random", filename->cstring, nullptr, MS_BIND, nullptr);

        // /dev/urandom
        s_setfn(filename, PATH_MAX, "%s/dev/urandom", build_directory->cstring);
        mkf_mount("/dev/urandom", filename->cstring, nullptr, MS_BIND, nullptr);

        s_free(filename);

        string_t *bin_sh = s_new_p(PATH_MAX);
        string_t *bin_env = s_new_p(PATH_MAX);
        string_t *env_var = s_new_p(STRING_DEFAULT_CAPACITY);

        // Now let's bind mount our dependencies
        mount_dependencies(derivative, build_directory, bin_sh, bin_env, env_var);

        // Let's check the invariants of /bin/sh and /usr/bin/env being able to be symlinked to run the build script
        if (bin_sh->length == 0)
        {
            fprintf(stderr, "No 'sh' or 'bash' program found in dependencies, the build script cannot be run\n");
            exit(EXIT_FAILURE);
        }

        if (bin_env->length == 0)
        {
            fprintf(stderr, "No 'env' program found in dependencies, the build script cannot be run\n");
            exit(EXIT_FAILURE);
        }

        // Now that we are past the point of anything that depends on the host filesystem, we can pivot root
        // First by setting up where we are going to put our old root
        string_t *old = s_cat_p(build_directory, "/old");
        checked_mkdir(old->cstring, 0755);

        // Then pivot
        if (syscall(SYS_pivot_root, build_directory->cstring, old->cstring) == -1)
        {
            fprintf(stderr, "Error pivoting root: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }
        s_free(old);

        // Then remove any way for the build script to affect our old root
        if (umount2("/old", MNT_DETACH) == -1)
        {
            fprintf(stderr, "Error detaching old root: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        if (rmdir("/old") == -1)
        {
            fprintf(stderr, "Error deleting inert old root folder: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // At this point we are a completely isolated "new" linux system from the host, so now we get ready to build

        // First by creating the build folder
        checked_mkdir("/build", 0755);
        if (chdir("/build") == -1)
        {
            fprintf(stderr, "Error changing directory to build: %s", strerror(errno));
            exit(EXIT_FAILURE);
        }

        // Then by creating the symlinks mentioned earlier
        checked_mkdir("/bin", 0755);
        if (symlink(bin_sh->cstring, "/bin/sh") == -1)
        {
            fprintf(stderr, "Error creating sh symlink: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        s_free(bin_sh);

        checked_mkdir("/usr", 0755);
        checked_mkdir("/usr/bin", 0755);
        if (symlink(bin_env->cstring, "/usr/bin/env") == -1)
        {
            fprintf(stderr, "Error creating env symlink: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        s_free(bin_env);

        // Then by creating our output directory
        string_t *out = s_own_p(get_derivative_store_path(&derivative->dheader));

        checked_mkdir(out->cstring, 0755);

        // Then by creating our packages environment variable
        s_pre(env_var, "packages=");

        // Then by creating our output environment variable
        s_pre(out, "out=");

        // Then *finally* by running our command
        char *env[] = {env_var->cstring, out->cstring, nullptr};
        char *cmd[] = {"/bin/sh", "-c", derivative->build, nullptr};

        execvpe(cmd[0], cmd, env);
        fprintf(stderr, "Executing build command failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    else
    {

        int status, code = EXIT_FAILURE;
        if (waitpid(builder, &status, 0) != -1)
        {
            if (WIFEXITED(status))
            {
                code = WEXITSTATUS(status);
                if (code != EXIT_SUCCESS)
                {
                    fprintf(stderr, "Builder process returned %d\n", code);
                }
            }
        }
        else
        {
            fprintf(stderr, "Error waiting for builder to finish: %s\n", strerror(errno));
        }

        exit(code);
    }
}

static int copy_file(FILE *from_file, FILE *to_file, char *buffer, size_t buffer_size)
{
    while (!feof(from_file))
    {
        if (ferror(from_file))
        {
            fprintf(stderr, "Error reading from file: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }

        size_t nread = fread(buffer, 1, buffer_size, from_file);
        fwrite(buffer, 1, nread, to_file);
        if (ferror(to_file))
        {

            fprintf(stderr, "Error writing to file: %s\n", strerror(errno));
            return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

static int monitor_jail(standard_derivative_t *derivative, string_t *build_directory, pid_t jail_process, int jail_pipe)
{
    // A buffer specifically for monitoring the jail
    static char monitor_buffer[4096];

    int status, code = EXIT_FAILURE;

    // 3 temporary strings that have to be deallocated at the end of the function
    string_t *store = s_fmt_p(PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name(&derivative->dheader));
    string_t *out_path = s_fmt_p(PATH_MAX, "%s%s", build_directory->cstring, get_derivative_store_path(&derivative->dheader));
    string_t *log = s_fmt_p(512, CALCULUS_LOGS_DIRECTORY "/%s.log", get_derivative_node_name(&derivative->dheader));

    // int file_fd = open(log->cstring, O_WRONLY| O_CREAT | O_TRUNC, 0666);
    FILE *log_file = fopen(log->cstring, "w+");
    if (log_file == nullptr)
    {
        fprintf(stderr, "Warning, unable to create log file: %s\n", strerror(errno));
        goto skip_logs;
    }
    FILE *jail_file = fdopen(jail_pipe, "r");
    int copy_code = copy_file(jail_file, log_file, monitor_buffer, sizeof(monitor_buffer));
    fclose(log_file);
    fclose(jail_file);
    if (copy_code != EXIT_SUCCESS)
        fprintf(stderr, "Warning, logs may be truncated\n");
skip_logs:
    if (waitpid(jail_process, &status, 0) != -1)
    {
        if (WIFEXITED(status))
        {
            code = WEXITSTATUS(status);
            if (code != EXIT_SUCCESS)
            {
                fprintf(stderr, "Builder process returned %d\n", code);
            }
        }
    }
    else
    {
        fprintf(stderr, "Error waiting for jail process to finish: %s\n", strerror(errno));
        return -1;
    }

    if (code == EXIT_SUCCESS)
    {
        // let us copy the file into our store and recursively make it readonly using libarchive
        // snprintf(store_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name(&standard->dheader));

        if (fs_isdir(out_path->cstring))
        {
            if (mkdir(store->cstring, 0777) == -1)
            {
                fprintf(stderr, "error creating directory in store: %s\n", strerror(errno));
                code = EXIT_FAILURE;
                goto true_finalize;
            }

            if (copy_directory(out_path->cstring, store->cstring) != ARCHIVE_OK)
            {
                code = EXIT_FAILURE;
                goto true_finalize;
            }

            if (chmod(store->cstring, 0555) == -1)
            {
                fprintf(stderr, "error chmod'ing the directory in store: %s\n", strerror(errno));
                code = EXIT_FAILURE;
                goto true_finalize;
            }
        }
        else if (fs_isreg(out_path->cstring))
        {
            struct stat stat_buf;
            if (stat(out_path->cstring, &stat_buf) == -1)
            {
                fprintf(stderr, "Error stat()ing output file: %s\n", strerror(errno));
                code = EXIT_FAILURE;
                goto true_finalize;
            }

            FILE *store_file = fopen(store->cstring, "w+");
            if (store_file == nullptr)
            {
                fprintf(stderr, "Error opening store file for writing: %s\n", strerror(errno));
                code = EXIT_FAILURE;
                goto true_finalize;
            }

            FILE *from_file = fopen(out_path->cstring, "r+");
            if (from_file == nullptr)
            {
                fprintf(stderr, "Error opening output file for reading: %s\n", strerror(errno));
                code = EXIT_FAILURE;
                goto true_finalize;
            }

            code = copy_file(from_file, store_file, monitor_buffer, sizeof(monitor_buffer));
            fclose(from_file);
            fclose(store_file);
            if (code != EXIT_SUCCESS)
                goto true_finalize;

            if (chmod(store->cstring, ((S_IXUSR & stat_buf.st_mode) > 0) ? 0555 : 0444) == -1)
            {
                fprintf(stderr, "Error chmod'ing the store file: %s\n", strerror(errno));
                code = EXIT_FAILURE;
            }
            goto true_finalize;
        }
        else
        {
            fprintf(stderr, "Output file is neither a regular file nor directory!\n");
            code = EXIT_FAILURE;
            goto true_finalize;
        }
    }
true_finalize:
    if (code != EXIT_SUCCESS)
    {
        fprintf(stderr, "Build failed - logs are at %s\n", log->cstring);
        string_t *cmd = s_fmt_p(PATH_MAX, "tail -n20 %s 2>&1", log->cstring);
        FILE *tail = popen(cmd->cstring, "r");
        s_free(cmd);
        if (tail == nullptr)
        {
            fprintf(stderr, "Cannot tail the logs...: %s", strerror(errno));
            goto cleanup;
        }
        fprintf(stderr, "Last 20 lines of the logs:\n");
        copy_file(tail, stderr, monitor_buffer, sizeof(monitor_buffer));
        pclose(tail);
    }
cleanup:
    s_free(store);
    s_free(out_path);
    s_free(log);
    return code;
}

static int build_standard(standard_derivative_t *standard)
{

    string_t *build_directory = s_fmt_p(512, "%s/%s", CALCULUS_BUILD_DIRECTORY, get_derivative_node_name(&standard->dheader));

    // We then remake any build directory
    fs_rmdir(build_directory->cstring);
    if (fs_ensure_dir(build_directory->cstring) == -1)
    {
        fprintf(stderr, "Error creating build directory for %s: %s\n", standard->name, strerror(errno));
        return -1;
    }

    // We set up pipes to capture our build proceses stdout
    int pipefd[2];

    if (pipe(pipefd) == -1)
    {
        fprintf(stderr, "Error opening log pipe for building derivative %s: %s", standard->name, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // We then fork to set up the build process
    pid_t child = fork();
    if (child < 0)
    {
        fprintf(stderr, "Fork failed when building directory for %s: %s\n", standard->name, strerror(errno));
        fs_rmdir(build_directory->cstring);
        return -1;
    }
    else if (child == 0)
    {
        // We are the process that will eventually build everything, so we quickly set up our monitoring stuff
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        int devnull = open("/dev/null", O_RDONLY);
        if (devnull < 0)
        {
            fprintf(stderr, "Error setting up /dev/null stdin for %s: %s\n", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        dup2(devnull, STDIN_FILENO);
        close(devnull);

        // Then enter jail
        enter_jail(standard, build_directory);
        unreachable();
    }
    else
    {
        close(pipefd[1]);
        int return_code = monitor_jail(standard, build_directory, child, pipefd[0]);
        fs_rmdir(build_directory->cstring);
        s_free(build_directory);
        return return_code;
    }
}

/******************************************************************************
 *
 * BUILD DISPATCHER
 *
 * build_derivative() - Dispatches a derivative to be built by the appropriate
 *                      build handler
 *
 *****************************************************************************/

int build_derivative(derivative_header_t *to_build)
{
    // (void)to_build;
    ensure_paths();
    switch (to_build->dtype)
    {
    case DT_STANDARD:
        return build_standard((standard_derivative_t *)to_build);
    case DT_FETCH_TARBALL:
        return build_tarball((fetch_tarball_derivative_t *)to_build);
    default:
        fprintf(stderr, "unknown derivative type!");
        return -1;
        break;
    }
}
