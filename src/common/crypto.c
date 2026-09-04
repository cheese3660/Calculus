#include "common/crypto.h"
#include "common/debug.h"
#include <string.h>
#include <math.h>

static sha256_t initial = {
    // (first 32 bits of the fractional parts of the square roots of the first 8 primes 2..19):
    {
        0x6a09e667U,
        0xbb67ae85U,
        0x3c6ef372U,
        0xa54ff53aU,
        0x510e527fU,
        0x9b05688cU,
        0x1f83d9abU,
        0x5be0cd19U}};

static uint32_t k[] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

sha256_t sha256_hash(const void *data, size_t len)
{
    debug("Hashing data of length %ld", len);
    sha256_ingest_t ingest = {};
    sha256_append(&ingest, data, len);
    return sha256_finalize(&ingest);
}

sha256_t sha256_hashf(FILE* file)
{
    debug("Hashing file");
    sha256_ingest_t ingest = {};
    sha256_appendf(&ingest, file);
    return sha256_finalize(&ingest);
}

const char *hex_chars = "0123456789abcdef";
static char hex_digest[65];
const char *sha256_to_hex(sha256_t *hash)
{
    for (int i = 0; i < 8; i++)
    {
        uint32_t val = hash->h[i];
        hex_digest[i * 8 + 0] = hex_chars[(val >> 28) & 0xF];
        hex_digest[i * 8 + 1] = hex_chars[(val >> 24) & 0xF];
        hex_digest[i * 8 + 2] = hex_chars[(val >> 20) & 0xF];
        hex_digest[i * 8 + 3] = hex_chars[(val >> 16) & 0xF];
        hex_digest[i * 8 + 4] = hex_chars[(val >> 12) & 0xF];
        hex_digest[i * 8 + 5] = hex_chars[(val >> 8) & 0xF];
        hex_digest[i * 8 + 6] = hex_chars[(val >> 4) & 0xF];
        hex_digest[i * 8 + 7] = hex_chars[(val >> 0) & 0xF];
    }
    hex_digest[64] = 0;
    return hex_digest;
}

const char *sha256_to_b64(sha256_t *hash)
{
    (void)hash;
    panic("NOT IMPLEMENTED");
    return nullptr;
}

// Rotate right macro, might want to take out, but really only used here
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
static void consume_chunk(sha256_ingest_t *ingest)
{
    uint32_t w[64];
    uint8_t *chunk = ingest->chunk;
    uint32_t *ih = ingest->result.h;

    for (int i = 0; i < 16; i++)
    {
        // We copy the chunk as 32 bit big endian words
        w[i] = ((uint32_t)chunk[i * 4 + 0] << 24) |
               ((uint32_t)chunk[i * 4 + 1] << 16) |
               ((uint32_t)chunk[i * 4 + 2] << 8) |
               ((uint32_t)chunk[i * 4 + 3] << 0);
    }

    for (int i = 16; i < 64; i++)
    {
        // Now we extend the w array
        uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = ih[0];
    uint32_t b = ih[1];
    uint32_t c = ih[2];
    uint32_t d = ih[3];
    uint32_t e = ih[4];
    uint32_t f = ih[5];
    uint32_t g = ih[6];
    uint32_t h = ih[7];

    // Now we compress
    for (int i = 0; i < 64; i++)
    {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + k[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ih[0] += a;
    ih[1] += b;
    ih[2] += c;
    ih[3] += d;
    ih[4] += e;
    ih[5] += f;
    ih[6] += g;
    ih[7] += h;

    ingest->chunk_ptr = 0;
    ingest->chunk_count += 1; // Used for the size calculation of the message at the end
}

static void append_unsafe(sha256_ingest_t *ingest, const uint8_t *data, size_t len)
{
    debug("Appending data of length %ld", len);

    while (len != 0)
    {
        size_t can_consume = 64 - ingest->chunk_ptr;
        size_t will_consume = len > can_consume ? can_consume : len;
        memcpy(&ingest->chunk[ingest->chunk_ptr], data, will_consume);
        ingest->chunk_ptr += will_consume;
        if (ingest->chunk_ptr == 64 /* 512 bits */)
        {
            consume_chunk(ingest);
        }
        len -= will_consume;
        data += will_consume;
    }
}

void sha256_append(sha256_ingest_t *ingest, const void *data, size_t len)
{
    if (ingest == nullptr)
        panic("ingest must not be null");
    if (data == nullptr)
        panic("data must not be null");
    if (ingest->chunk_count == (uint64_t)-1)
        panic("ingest must not already be completed!");
    if (ingest->chunk_ptr == 0 && ingest->chunk_count == 0)
        ingest->result = initial;

    append_unsafe(ingest, data, len);
}

void sha256_appendf(sha256_ingest_t *ingest, FILE *file)
{
    // The 4 kilobyte buffer that is used for ingesting data
    static uint8_t buffer[4096];

    if (ingest == nullptr)
        panic("ingest must not be null");
    if (file == nullptr)
        panic("data must not be null");

    if (ingest->chunk_count == (uint64_t)-1)
        panic("ingest must not already be completed!");
    if (ingest->chunk_ptr == 0 && ingest->chunk_count == 0)
        ingest->result = initial;

    // Read until EOF
    while (!feof(file))
    {
        size_t read = fread(buffer, 1, 4096, file);
        if (read != 4096 && ferror(file)) {
            perror("fread");
            panic("Reading file failed in appendf"); // TODO make this actually return a value
        }
        append_unsafe(ingest, buffer, read);
    }
}

sha256_t sha256_finalize(sha256_ingest_t *ingest)
{
    if (ingest == nullptr)
        panic("ingest must not be null");
    if (ingest->chunk_count == (uint64_t)-1)
        panic("ingest must not already be completed!");
    if (ingest->chunk_ptr == 0 && ingest->chunk_count == 0)
    {
        ingest->result = initial;
    }

    debug("Finalizing ingest!");
    uint64_t length = (ingest->chunk_count * 64 + ingest->chunk_ptr) * 8;
    debug("Length = %ld", length);
    size_t bytes_in_block = ingest->chunk_ptr;
    size_t remaining = (bytes_in_block < 56) ? (64 - bytes_in_block) : (128 - bytes_in_block);
    uint8_t *data = malloc(remaining);
    memset(data, 0, remaining);
    data[0] = 0x80;
    data[remaining - 8] = 0xff & (length >> 56);
    data[remaining - 7] = 0xff & (length >> 48);
    data[remaining - 6] = 0xff & (length >> 40);
    data[remaining - 5] = 0xff & (length >> 32);
    data[remaining - 4] = 0xff & (length >> 24);
    data[remaining - 3] = 0xff & (length >> 16);
    data[remaining - 2] = 0xff & (length >> 8);
    data[remaining - 1] = 0xff & (length);
    sha256_append(ingest, data, remaining);
    free(data);
    ingest->chunk_count = (uint64_t)-1;
    return ingest->result;
}