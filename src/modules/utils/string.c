#include <osapi.h>
#include <mem.h>

#include "modules/utils/string.h"


/******************************************************************************
 * Calculate string offset related to other string.
 *
 *******************************************************************************/
int ICACHE_FLASH_ATTR
index_of(char *base, char *str, int start)
{
    char *find = (char *)os_strstr(base + start, str);
    if (find != NULL)
        return (find - base);
    return -1;
}

/******************************************************************************
 * Check string starts with string.
 *
 *******************************************************************************/
bool ICACHE_FLASH_ATTR
starts_with(char* base, char* str)
{
    return (os_strstr(base, str) - base) == 0;
}

/******************************************************************************
 * Check string ends with string.
 *
 *******************************************************************************/
bool ICACHE_FLASH_ATTR
ends_with(char* base, char* str)
{
    int blen = os_strlen(base);
    int slen = os_strlen(str);
    return (blen >= slen) && (0 == os_strcmp(base + blen - slen, str));
}

/******************************************************************************
 * Split string based on substring returning the splits.
 *
 *******************************************************************************/
void ICACHE_FLASH_ATTR
split(char *base, char *str, char *splits[])
{
    // Make a copy because strok changes it
    int len = os_strlen(base);
    char *cp  = (char *) os_zalloc(len + 1);
    os_memcpy(cp, base, len);

    int max = sizeof(splits);
    int i = 0;
    char *token = strtok(cp, str);
    while (token != NULL && i < max)
    {
        splits[i] = token;
        ++i;
        token = strtok(NULL, str);
    }
    os_free(cp);
}
