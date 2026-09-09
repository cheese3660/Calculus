// This is the main calculus build file

// TODO: Convert this into it's own program - calculus-integrate
// That when given a derivative recipe in string form, integrates all it's requirements

#define _GNU_SOURCE

#include "common/fs.h"
#include "common/debug.h"

#include "calculus/build.h"
#include "calculus/paths.h"

#include <curl/curl.h>

#include <unistd.h>
#include <stdlib.h>

#include <sys/stat.h>

#include <errno.h>

#include <archive.h>

#include <archive_entry.h>

#include <sys/wait.h>

#include <sched.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <grp.h>
#include <sys/syscall.h>

#include <common/archive.h>

#include "thirdparty/stb_ds.h"

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

static char archive_fullpath[PATH_MAX * 2];
static char archive_targetpath[PATH_MAX * 2];
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
        snprintf(archive_fullpath, sizeof(archive_fullpath), "%s/%s", directory, current_path);
        archive_entry_set_pathname(entry, archive_fullpath);

        const char *hardlink_target = archive_entry_hardlink(entry);
        if (hardlink_target != nullptr)
        {
            snprintf(archive_targetpath, sizeof(archive_targetpath), "%s/%s", directory, hardlink_target);
            archive_entry_set_hardlink(entry, archive_targetpath);
        }

        // Make the permissions readonly while extracting
        archive_entry_set_perm(entry, current_perms & ~0222);
        if ((result = archive_write_header(to, entry)))
        {
            fprintf(stderr, "Warning reading entry %s: %s\n", archive_fullpath, archive_error_string(to));
        }
        else if (archive_entry_size(entry) > 0 && (result = copy_archive(from, to)))
        {
            fprintf(stderr, "Error writing entry %s\n", archive_fullpath);
        }

        archive_write_finish_entry(to);
    }

    archive_read_close(from);
    archive_read_free(from);
    archive_write_close(to);
    archive_write_free(to);

    return result;
}

static int copy_directory_readonly(const char *from_directory, const char *directory)
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
        snprintf(archive_fullpath, sizeof(archive_fullpath), "%s/%s", directory, current_path);
        archive_entry_set_pathname(entry, archive_fullpath);

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

            snprintf(archive_targetpath, sizeof(archive_targetpath), "%s/%s", directory, hardlink_target);
            archive_entry_set_hardlink(entry, archive_targetpath);
        }

        // Make the permissions readonly while extracting
        archive_entry_set_perm(entry, current_perms & ~0222);
        if ((result = archive_write_header(to, entry)))
        {
            fprintf(stderr, "Warning reading entry %s: %s\n", archive_fullpath, archive_error_string(to));
        }
        else if (archive_entry_size(entry) > 0 && (result = copy_archive(from, to)))
        {
            fprintf(stderr, "Error writing entry %s\n", archive_fullpath);
        }

        archive_write_finish_entry(to);
    }

    archive_read_close(from);
    archive_read_free(from);
    archive_write_close(to);
    archive_write_free(to);

    return result;
}

static int build_tarball(fetch_tarball_derivative_t *tarball)
{
    static char fp_buffer[512 /* We can assume a lot less of a size here because there is a max size on paths*/];

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

    snprintf(fp_buffer, 512, "%s/%s", CALCULUS_STORE_DIRECTORY, get_derivative_node_name((derivative_header_t *)tarball));

    if (!tarball->extract)
    {
        outfile_name = fp_buffer;
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
    if (mkdir(fp_buffer, 0777) == -1)
    {
        fprintf(stderr, "error creating output directory for built tarball %s: %s\n", tarball->url, strerror(errno));
        remove(outfile_name);
        goto done;
    }

    if (extract_archive(outfile_name, fp_buffer) != ARCHIVE_OK)
    {
        fprintf(stderr, "error extracting tarball %s\n",tarball->url);
        remove(outfile_name);
        goto done;
    }

    // Mark as read only
    chmod(fp_buffer, 0555);
    // And owned by root
    if (chown(fp_buffer, 0, 0) == -1)
    {
        fprintf(stderr, "error changing owner of output directory: %s\n", strerror(errno));
        remove(outfile_name);
        goto done;
    }
    result = 0;
    goto done;

    // We remove the original downloaded temp file
    remove(outfile_name);

    result = 0;
done:
    if (fp)
        fclose(fp);
    if (curl)
        curl_easy_cleanup(curl);
    return result;
}

static int build_standard(standard_derivative_t *standard)
{
    static char fp_buffer[512 /* We can assume a lot less of a size here because there is a max size on paths*/];
    static char log_buffer[512];
    static char chroot_path_buffer[PATH_MAX];
    static char store_path_buffer[PATH_MAX];

    char *out_path = nullptr;

    // We first create the build directory that will be our chroot
    snprintf(fp_buffer, 512, "%s/%s", CALCULUS_BUILD_DIRECTORY, get_derivative_node_name(&standard->dheader));

    if (asprintf(&out_path, "%s%s", fp_buffer, get_derivative_store_path(&standard->dheader)) == -1)
    {
        perror("asprintf");
        return -1;
    }

    // We then make sure no build directory currently exists
    fs_rmdir(fp_buffer);
    if (fs_ensure_dir(fp_buffer) == -1)
    {
        fprintf(stderr, "Error creating build directory for %s: %s\n", standard->name, strerror(errno));
        return -1;
    }

    // We fork to set up the build process

    int pipefd[2];

    if (pipe(pipefd) == -1)
    {
        fprintf(stderr, "Error opening log pipe for building derivative %s: %s", standard->name, strerror(errno));
        exit(EXIT_FAILURE);
    }
    pid_t child = fork();
    if (child < 0)
    {
        fprintf(stderr, "Fork failed when building directory for %s: %s\n", standard->name, strerror(errno));
        fs_rmdir(fp_buffer);
        return -1;
    }
    else if (child == 0)
    {

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

        // set up UID maps or else we can't create a tmpfs
        uid_t uid = getuid();
        gid_t gid = getgid();
        // We are the child here

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
            fprintf(stderr, "Error setting up unshare for %s: %s\n", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }

        char map_buf[32];
        int fd = open("/proc/self/setgroups", O_WRONLY);
        if (fd == -1)
        {
            fprintf(stderr, "Error setting up uid map (setgroups) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (write(fd, "deny", 4) == -1)
        {
            fprintf(stderr, "Error writing to uid map (setgroups) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        close(fd);

        fd = open("/proc/self/uid_map", O_WRONLY);
        if (fd == -1)
        {
            fprintf(stderr, "Error setting up uid map (uid_map) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        snprintf(map_buf, sizeof(map_buf), "0 %d 1\n", uid);
        if (write(fd, map_buf, strlen(map_buf)) == -1)
        {
            fprintf(stderr, "Error writing to uid map (uid_map) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        close(fd);

        fd = open("/proc/self/gid_map", O_WRONLY);
        if (fd == -1)
        {
            fprintf(stderr, "Error setting up uid map (gid_map) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        snprintf(map_buf, sizeof(map_buf), "0 %d 1\n", gid);
        if (write(fd, map_buf, strlen(map_buf)) == -1)
        {
            fprintf(stderr, "Error writing to uid map (gid_map) for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        close(fd);

        pid_t builder = fork();
        if (builder < 0)
        {
            fprintf(stderr, "Error forking builder process for %s: %s", standard->name, strerror(errno));
            exit(EXIT_FAILURE);
        }
        else if (builder == 0)
        {

            // WE HAVE TO DO ALL THIS SETUP IN THE FORKED PLACE

            if (mount("none", "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1)
            {
                fprintf(stderr, "Error making mounts private for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (mount(fp_buffer, fp_buffer, NULL, MS_BIND | MS_REC, NULL) == -1)
            {
                fprintf(stderr, "Error creating mount point for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // Virtual file systems
            snprintf(chroot_path_buffer, PATH_MAX, "%s/proc", fp_buffer);
            if (mkdir(chroot_path_buffer, 0755) == -1)
            {
                fprintf(stderr, "Error creating proc filesystem for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (mount("proc", chroot_path_buffer, "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/sys", fp_buffer);
            if (mkdir(chroot_path_buffer, 0755) == -1)
            {
                fprintf(stderr, "Error creating sys filesystem for %s: %s", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (mount("sysfs", chroot_path_buffer, "sysfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/tmp", fp_buffer);
            if (mkdir(chroot_path_buffer, 0755) == -1)
            {
                fprintf(stderr, "Error creating tmp filesystem for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            if (mount("tmpfs", chroot_path_buffer, "tmpfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, "size=512M,mode=1777") == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // safe devices
            snprintf(chroot_path_buffer, PATH_MAX, "%s/dev", fp_buffer);

            if (mkdir(chroot_path_buffer, 0755) == -1)
            {
                fprintf(stderr, "Error creating dev filesystem for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (mount("tmpfs", chroot_path_buffer, "tmpfs", MS_NOSUID | MS_NOATIME, "mode=0755,uid=0,gid=0") == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/dev/null", fp_buffer);

            // debug("%s", chroot_path_buffer);
            int nul_fd = open(chroot_path_buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
            if (nul_fd == -1)
            {
                fprintf(stderr, "Error creating %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(nul_fd);

            if (mount("/dev/null", chroot_path_buffer, NULL, MS_BIND, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/dev/zero", fp_buffer);

            int zer_fd = open(chroot_path_buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
            if (zer_fd == -1)
            {
                fprintf(stderr, "Error creating %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(zer_fd);

            if (mount("/dev/zero", chroot_path_buffer, NULL, MS_BIND, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/dev/random", fp_buffer);

            int rnd_fd = open(chroot_path_buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
            if (rnd_fd == -1)
            {
                fprintf(stderr, "Error creating %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(rnd_fd);

            if (mount("/dev/random", chroot_path_buffer, NULL, MS_BIND, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/dev/urandom", fp_buffer);

            int urnd_fd = open(chroot_path_buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0666);
            if (urnd_fd == -1)
            {
                fprintf(stderr, "Error creating %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            close(urnd_fd);

            if (mount("/dev/urandom", chroot_path_buffer, NULL, MS_BIND, NULL) == -1)
            {
                fprintf(stderr, "Error mounting %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // So now we bind mount our dependencies
            char *sh_path = nullptr;
            char *env_path = nullptr;

            snprintf(chroot_path_buffer, PATH_MAX, "%s" CALCULUS_TRUE_PREFIX, fp_buffer);
            if (mkdir(chroot_path_buffer, 0555) == -1)
            {
                fprintf(stderr, "Error creating %s directory for %s: %s\n", CALCULUS_TRUE_PREFIX, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s" CHROOT_STORE_DIRECTORY, fp_buffer);
            if (mkdir(chroot_path_buffer, 0555) == -1)
            {
                fprintf(stderr, "Error creating %s directory for %s: %s\n", CHROOT_STORE_DIRECTORY, standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            char **packages = nullptr;
            size_t combined_count = 0;

            for (size_t i = 0; i < standard->num_dependencies; i++)
            {
                // Now we bind all our dependencies
                derivative_header_t *dep = standard->dependencies[i];
                snprintf(store_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name(dep));
                snprintf(chroot_path_buffer, PATH_MAX, "%s" CHROOT_STORE_DIRECTORY "/%s", fp_buffer, get_derivative_node_name(dep));
                if (fs_isreg(store_path_buffer))
                {
                    // This is a store path, we just want to bind and continue
                    int fd = open(chroot_path_buffer, O_WRONLY | O_CREAT, 0644);
                    if (fd == -1)
                    {
                        fprintf(stderr, "Error creating dependency %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                    close(fd);

                    if (mount(store_path_buffer, chroot_path_buffer, nullptr, MS_BIND | MS_RDONLY, nullptr) == -1)
                    {
                        fprintf(stderr, "Error binding dependency %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                    goto add_to_env_var;
                }

                if (!fs_isdir(store_path_buffer))
                {
                    fprintf(stderr, "Error binding dependency %s for %s: dependency is not a file or directory\n", store_path_buffer, standard->name);
                    exit(EXIT_FAILURE);
                }

                if (mkdir(chroot_path_buffer, 0644) == -1)
                {
                    fprintf(stderr, "Error creating dependency %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                    exit(EXIT_FAILURE);
                }

                if (mount(store_path_buffer, chroot_path_buffer, nullptr, MS_BIND | MS_RDONLY, nullptr) == -1)
                {
                    fprintf(stderr, "Error binding dependency %s for %s: %s\n", chroot_path_buffer, standard->name, strerror(errno));
                    exit(EXIT_FAILURE);
                }

                // Collect the 2 things we need to make symlinks for
                if (sh_path == nullptr)
                {
                    snprintf(chroot_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s/bin/bash", get_derivative_node_name(dep));
                    if (fs_exists(chroot_path_buffer))
                    {
                        if (asprintf(&sh_path, "%s/bin/bash", get_derivative_store_path(dep)) == -1)
                        {
                            // These shouldn't fail so
                            perror("asprintf");
                            exit(EXIT_FAILURE);
                        }
                    }
                    else
                    {
                        snprintf(chroot_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s/bin/sh", get_derivative_node_name(dep));
                        if (fs_exists(chroot_path_buffer))
                        {
                            if (asprintf(&sh_path, "%s/bin/sh", get_derivative_store_path(dep)) == -1)
                            {
                                perror("asprintf");
                                exit(EXIT_FAILURE);
                            }
                        }
                    }
                }

                if (env_path == nullptr)
                {
                    snprintf(chroot_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s/bin/env", get_derivative_node_name(dep));

                    if (fs_exists(chroot_path_buffer))
                    {
                        if (asprintf(&env_path, "%s/bin/env", get_derivative_store_path(dep)) == -1)
                        {
                            perror("asprintf");
                            exit(EXIT_FAILURE);
                        }
                    }
                }
            add_to_env_var:
                char *pkg = strdup(get_derivative_store_path(dep));
                combined_count += strlen(pkg) + 1;
                arrpush(packages, pkg);
            }

            if (env_path == nullptr)
            {
                fprintf(stderr, "dependencies for %s don't contain an env binary, this is unsupported!\n", standard->name);
                exit(EXIT_FAILURE);
            }

            if (sh_path == nullptr)
            {
                fprintf(stderr, "dependencies for %s don't contain an sh/bash binary, this is unsupported!\n", standard->name);
                exit(EXIT_FAILURE);
            }

            // Now we set up our /bin/sh and /usr/bin/env parent folders
            snprintf(chroot_path_buffer, PATH_MAX, "%s/bin", fp_buffer);
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating bin folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/usr", fp_buffer);
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating usr folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/usr/bin", fp_buffer);
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating usr/bin folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            snprintf(chroot_path_buffer, PATH_MAX, "%s/usr/bin/env", fp_buffer);

            // And create the build folder
            snprintf(chroot_path_buffer, PATH_MAX, "%s/build", fp_buffer);
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating build folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // Then create the output directory as well
            snprintf(chroot_path_buffer, PATH_MAX, "%s%s", fp_buffer, get_derivative_store_path(&standard->dheader));
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating output folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // NOW THAT WE ARE HERE, WE PIVOT SETUP SYMLINKS AND EXEC

            snprintf(chroot_path_buffer, PATH_MAX, "%s/old", fp_buffer);
            if (mkdir(chroot_path_buffer, 0777) == -1)
            {
                fprintf(stderr, "Error creating old root folder for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (syscall(SYS_pivot_root, fp_buffer, chroot_path_buffer) == -1)
            {
                fprintf(stderr, "Error pivoting root for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (chdir("/build") == -1)
            {
                fprintf(stderr, "Error chdiring for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (umount2("/old", MNT_DETACH) == -1)
            {
                fprintf(stderr, "Error umounting old root for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }
            rmdir("/old");

            // SETUP THE SYMLINKS
            if (symlink(sh_path, "/bin/sh") == -1)
            {
                fprintf(stderr, "Error creating sh symlink for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            if (symlink(env_path, "/usr/bin/env") == -1)
            {
                fprintf(stderr, "Error creating env symlink for %s: %s\n", standard->name, strerror(errno));
                exit(EXIT_FAILURE);
            }

            // TODO: Make this more efficient
            char *packages_var = malloc(strlen("packages=") + combined_count + 1);
            strcpy(packages_var, "packages=");
            for (size_t i = 0; i < (size_t)arrlen(packages); i++)
            {
                if (i > 0)
                {
                    strcat(packages_var, ":");
                }
                strcat(packages_var, packages[i]);
            }

            char *out_var = nullptr;
            if (asprintf(&out_var, "out=%s", get_derivative_store_path(&standard->dheader)) == -1)
            {
                perror("asprintf");
                exit(EXIT_FAILURE);
            }

            // This is the environment
            char *env[] = {packages_var, out_var, nullptr};
            char *cmd[] = {"/bin/sh", "-c", standard->build, nullptr};

            execvpe(cmd[0], cmd, env);
            perror("execvpe");
            exit(EXIT_FAILURE);
        }
        else
        {
            int status, code = EXIT_FAILURE;
            if (waitpid(child, &status, 0) != -1)
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
                fprintf(stderr, "Error waiting for builder to finish for %s: %s\n", standard->name, strerror(errno));
            }

            exit(code);
        }
    }
    else
    {
        close(pipefd[1]);

        // Let's save the log here
        int status, code = EXIT_FAILURE;
        snprintf(log_buffer, 512, "%s/%s.log", CALCULUS_LOGS_DIRECTORY, get_derivative_node_name(&standard->dheader));
        int file_fd = open(log_buffer, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        ssize_t n;
        if (file_fd == -1)
        {
            fprintf(stderr, "Warning, unable to create logs for %s: %s\n", standard->name, strerror(errno));
            goto skip_logs;
        }

        // Let's just reuse the store path buffer
        while ((n = read(pipefd[0], store_path_buffer, PATH_MAX)) > 0)
        {
            char *ptr = store_path_buffer;
            while (n > 0)
            {
                ssize_t written = write(file_fd, ptr, n);
                if (written < 0)
                {
                    fprintf(stderr, "Warning, logs possibly truncated for %s: %s\n", standard->name, strerror(errno));
                    close(file_fd);
                    goto finalize;
                }
                n -= written;
                ptr += written;
            }
        }

        if (n < 0)
        {
            fprintf(stderr, "Warning, logs possibly truncated for %s: %s\n", standard->name, strerror(errno));
        }
        close(file_fd);
    skip_logs:

        if (waitpid(child, &status, 0) != -1)
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
            fprintf(stderr, "Error waiting for builder to finish for %s: %s\n", standard->name, strerror(errno));

            // Remove the build folder as soon as we can
            fs_rmdir(fp_buffer);
            return -1;
        }
        // Let's save the log either way


    finalize:
        if (code == EXIT_SUCCESS)
        {
            // let us copy the file into our store and recursively make it readonly using libarchive
            snprintf(store_path_buffer, PATH_MAX, CALCULUS_STORE_DIRECTORY "/%s", get_derivative_node_name(&standard->dheader));
            if (fs_isdir(out_path))
            {
                if (mkdir(store_path_buffer, 0777) == -1)
                {
                    fprintf(stderr, "error creating directory in store: %s\n", strerror(errno));
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }

                if (copy_directory_readonly(out_path, store_path_buffer) != ARCHIVE_OK)
                {
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }

                if (chmod(store_path_buffer, 0555) == -1)
                {
                    fprintf(stderr, "error chmod'ing the directory in store: %s\n", strerror(errno));
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }
            }
            else if (fs_isreg(out_path))
            {
                struct stat stat_buf;
                if (stat(out_path, &stat_buf) == -1)
                {
                    fprintf(stderr, "Error stat()ing output file: %s\n", strerror(errno));
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }

                FILE *store_file = fopen(store_path_buffer, "w+");
                if (store_file == nullptr)
                {
                    fprintf(stderr, "Error opening store file for writing: %s\n", strerror(errno));
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }

                FILE *from_file = fopen(out_path, "r+");
                if (from_file == nullptr)
                {
                    fprintf(stderr, "Error opening output file for reading: %s\n", strerror(errno));
                    code = EXIT_FAILURE;
                    goto true_finalize;
                }

                // This should just be a simple file->file transfer
                while (!feof(from_file))
                {
                    if (ferror(from_file))
                    {
                        fprintf(stderr, "Error reading from output file: %s\n", strerror(errno));
                        code = EXIT_FAILURE;
                        goto true_finalize;
                    }

                    size_t nread = fread(chroot_path_buffer, 1, PATH_MAX, from_file);
                    fwrite(chroot_path_buffer, 1, nread, store_file);
                    if (ferror(store_file))
                    {

                        fprintf(stderr, "Error writing to store file: %s\n", strerror(errno));
                        code = EXIT_FAILURE;
                        goto true_finalize;
                    }
                }
                fclose(from_file);
                fclose(store_file);

                chmod(store_path_buffer, ((S_IXUSR & stat_buf.st_mode) > 0) ? 0555 : 0444);
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
        fs_rmdir(fp_buffer);
        if (code != EXIT_SUCCESS)
        {
            fprintf(stderr, "Build failed - logs are at %s\n", log_buffer);
            return -1;
        }
    }
    
    return 0;
}

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