
#include <lua.h>
#include "crypto.h"
#include "stdio.h"

#define SHA256(X) do {\
    sha256_t x = sha256_string(X);\
    printf("SHA256('" X "') = %s\n", sha256_to_hex(&x));\
} while (0)

int main() {
    SHA256("");
    SHA256("abc");
    SHA256("hello world");
    SHA256("foobar");

    SHA256("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456");
    SHA256("0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF01234567");
}