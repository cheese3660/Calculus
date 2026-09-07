// This is the main calculus build file

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
    static char fullpath[PATH_MAX];
    static char targetpath[PATH_MAX];
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
        snprintf(fullpath, PATH_MAX, "%s/%s", directory, current_path);
        archive_entry_set_pathname(entry, fullpath);

        const char* hardlink_target = archive_entry_hardlink(entry);
        if (hardlink_target != nullptr) {
            snprintf(targetpath, PATH_MAX, "%s/%s", directory, hardlink_target);
            archive_entry_set_hardlink(entry, targetpath);
        }

        // Make the permissions readonly while extracting
        archive_entry_set_perm(entry, current_perms & ~0222);
        if ((result = archive_write_header(to, entry)))
        {
            fprintf(stderr, "Warning reading entry %s: %s\n", fullpath, archive_error_string(to));
        }
        else if (archive_entry_size(entry) > 0 && (result = copy_archive(from, to)))
        {
            fprintf(stderr, "Error writing entry %s\n", fullpath);
        }
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
        fprintf(stderr, "error opening file to download tarball to: %s\n", strerror(errno));
        goto done;
    }

    curl_easy_setopt(curl, CURLOPT_URL, tarball->url);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode error = curl_easy_perform(curl);
    if (error != CURLE_OK)
    {
        fprintf(stderr, "error downloading tarball via curl: %s\n", curl_easy_strerror(error));
        goto done;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400)
    {
        fprintf(stderr, "downloading tarball failed with code %ld\n", http_code);
        goto done;
    }

    fclose(fp);
    fp = fopen(outfile_name, "r");
    if (!fp)
    {
        fprintf(stderr, "error opening tarball after downloading: %s\n", strerror(errno));
        goto done;
    }
    sha256_t file_hash = sha256_hashf(fp);
    fclose(fp);
    fp = nullptr;
    if (sha256_cmp(&file_hash, &tarball->dheader.dhash) != 0)
    {
        fprintf(stderr, "tarball hash is not what was expected!\n");
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
            fprintf(stderr, "error changing owner of tarball file: %s\n", strerror(errno));
            remove(outfile_name);
            goto done;
        }
        result = 0;
        goto done;
    }

    // Now let's make our directory
    if (mkdir(fp_buffer, 0777) == -1)
    {
        fprintf(stderr, "error creating output directory for built tarball: %s\n", strerror(errno));
        remove(outfile_name);
        goto done;
    }

    if (extract_archive(outfile_name, fp_buffer) != ARCHIVE_OK)
    {
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


int build_derivative(derivative_header_t *to_build)
{
    // (void)to_build;
    ensure_paths();
    switch (to_build->dtype)
    {
        case DT_STANDARD:
            fprintf(stderr, "standard builds aren't implemented yet!\n");
            return -1;
        case DT_FETCH_TARBALL:
            return build_tarball((fetch_tarball_derivative_t*)to_build);
            break;
        default:
            fprintf(stderr, "unknown derivative type!");
            return -1;
            break;
    }
}