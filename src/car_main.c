#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "common/debug.h"
#include "common/archive.h"

void usage(void)
{
    fprintf(stderr, "Usage: car <options> <path>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    options     any of the options described below\n");
    fprintf(stderr, "    path        the file/directory being processed\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "    OPTIONS\n");
    fprintf(stderr, "    -a          Archive <path> to out.car\n");
    fprintf(stderr, "    -A <out>    Archive <path> to <out>\n");
    fprintf(stderr, "    -x          Extract <path> to the cwd\n");
    fprintf(stderr, "    -X <out>    Extract <path> into <out>, creating it if it doesn't exist\n");
    fprintf(stderr, "    -H          Print the sha256 sum of <path>\n");
    fprintf(stderr, "    -h          Print this message to stderr and quit\n");
}

#define USAGE_ERROR(fmt, ...)                                 \
    do                                                        \
    {                                                         \
        fprintf(stderr, "Error: " fmt "\n\n", ##__VA_ARGS__); \
        usage();                                              \
        exit(EXIT_FAILURE);                                   \
    } while (0)

#define ERROR(fmt, ...)                                     \
    do                                                      \
    {                                                       \
        fprintf(stderr, "Error: " fmt "\n", ##__VA_ARGS__); \
        exit(EXIT_FAILURE);                                 \
    } while (0)

#define PERROR(str)         \
    do                      \
    {                       \
        perror(str);        \
        exit(EXIT_FAILURE); \
    } while (0)

typedef enum
{
    MODE_UNKNOWN,
    MODE_ARCHIVE,
    MODE_EXTRACT,
    MODE_HASH
} car_mode_t;

static void hash(const char *path)
{
    struct stat res;
    int s = stat(path, &res);
    if (s == -1)
        PERROR("stat");

    if (S_ISDIR(res.st_mode))
    {
        sha256_t hash;
        car_hash(path, &hash);
        printf("%s\n", sha256_to_hex(&hash));
    }
    else if (S_ISREG(res.st_mode))
    {
        FILE *f = fopen(path, "r");
        if (f == nullptr)
            PERROR("fopen");

        sha256_t hash = sha256_hashf(f);
        printf("%s\n", sha256_to_hex(&hash));
    }
    else
    {
        ERROR("unsupported file type");
    }
}

static void archive(const char *path, const char *outpath)
{
    FILE *out = fopen(outpath, "w");
    if (out == nullptr)
        PERROR("fopen");
    
    int res = car_archive(path, out);
    
    if (res != 0)
        exit(EXIT_FAILURE);

    fclose(out);
}

static void ensure_directory(const char* path) {
    if (mkdir(path, 0777) == 0) return;
    if (errno == EEXIST) return;
    PERROR("mkdir");
}

static void extract(const char* car_path, const char* outdir)
{
    ensure_directory(outdir);

    FILE* in = fopen(car_path, "r");
    if (in == nullptr)
        PERROR("fopen");
    
    car_extract(in, outdir);
}

int main(int argc, char **argv)
{
    car_mode_t mode = MODE_UNKNOWN;
    const char *out_file = "out.car";
    const char *out_dir = ".";
    const char *path;
    const char *opts = "aA:xX:Hh";
    for (int opt = getopt(argc, argv, opts); opt != -1; opt = getopt(argc, argv, opts))
    {
        switch (opt)
        {
        case 'A':
            if (optarg == nullptr)
            {
                USAGE_ERROR("-A takes an argument");
            }
            out_file = optarg;
            [[fallthrough]];
        case 'a':
            mode = MODE_ARCHIVE;
            break;
        case 'X':
            if (optarg == nullptr)
            {
                USAGE_ERROR("-X takes an argument");
            }
            out_dir = optarg;
            [[fallthrough]];
        case 'x':
            mode = MODE_EXTRACT;
            break;
        case 'H':
            mode = MODE_HASH;
            break;
        case 'h':
            usage();
            exit(EXIT_SUCCESS);
        default:
            fprintf(stderr, "\n");
            usage();
            exit(EXIT_SUCCESS);
        }
    }

    if (optind == argc)
    {
        USAGE_ERROR("missing path to process");
    }

    path = argv[optind];

    switch (mode)
    {
    case MODE_ARCHIVE:
        archive(path, out_file);
        break;
    case MODE_EXTRACT:
        extract(path, out_dir);
        break;
    case MODE_HASH:
        hash(path);
        break;
    default:
        USAGE_ERROR("expected mode option");
        break;
    }
}