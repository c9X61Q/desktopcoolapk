// C/C++ 双用 shim：给 C++ 提供稳定 bcrypt 入口
#include <string.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

extern char *_crypt_blowfish_rn(const char *key, const char *setting, char *output, int size);

char *ka_crypt_rn(const char *key, const char *setting, char *out, int size) {
    return _crypt_blowfish_rn(key, setting, out, size);
}

#ifdef __cplusplus
}
#endif
