/* minimal program to link libcrypto */
#include <openssl/sha.h>
int main()
{
  SHA_CTX ctx;
  SHA1_Init (&ctx);
  return 0;
}
