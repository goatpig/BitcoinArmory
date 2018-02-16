/* Copyright 2015 BitPay, Inc.
 * Distributed under the MIT/X11 software license, see the accompanying
 * file COPYING or http://www.opensource.org/licenses/mit-license.php.
 */

#include <btc/cstr.h>

#include <stdlib.h>
#include <string.h>

static int cstr_alloc_min_sz(cstring* s, size_t sz)
{
    unsigned int shift;
    unsigned int al_sz;
    char* new_s;

    sz++; /* NULL overhead */

    if (s->alloc && (s->alloc >= sz))
        return 1;

    shift = 3;
    while ((al_sz = (1 << shift)) < sz)
        shift++;

    new_s = btc_realloc(s->str, al_sz);
    if (!new_s)
        return 0;

    s->str = new_s;
    s->alloc = al_sz;
    s->str[s->len] = 0;

    return 1;
}

int cstr_alloc_minsize(cstring* s, size_t new_sz)
{
    /* no change */
    if (new_sz == s->len)
        return 1;

    /* truncate string */
    if (new_sz <= s->len) {
        return 0;
    }

    /* increase string size */
    if (!cstr_alloc_min_sz(s, new_sz))
        return 0;

    /* contents of string tail undefined */
    //s->len = new_sz;
    s->str[s->len] = 0;

    return 1;
}

cstring* cstr_new_sz(size_t sz)
{
    cstring* s = btc_calloc(1, sizeof(cstring));
    if (!s)
        return NULL;

    if (!cstr_alloc_min_sz(s, sz)) {
        btc_free(s);
        return NULL;
    }

    return s;
}

cstring* cstr_new_buf(const void* buf, size_t sz)
{
    cstring* s = cstr_new_sz(sz);
    if (!s)
        return NULL;

    memcpy(s->str, buf, sz);
    s->len = sz;
    s->str[s->len] = 0;

    return s;
}

cstring* cstr_new_cstr(const cstring* copy_str)
{
    return cstr_new_buf(copy_str->str, copy_str->len);
}

cstring* cstr_new(const char* init_str)
{
    size_t slen;

    if (!init_str || !*init_str)
        return cstr_new_sz(0);

    slen = strlen(init_str);
    return cstr_new_buf(init_str, slen);
}

void cstr_free(cstring* s, int free_buf)
{
    if (!s)
        return;

    if (free_buf)
        btc_free(s->str);

    memset(s, 0, sizeof(*s));
    btc_free(s);
}

int cstr_resize(cstring* s, size_t new_sz)
{
    /* no change */
    if (new_sz == s->len)
        return 1;

    /* truncate string */
    if (new_sz <= s->len) {
        s->len = new_sz;
        s->str[s->len] = 0;
        return 1;
    }

    /* increase string size */
    if (!cstr_alloc_min_sz(s, new_sz))
        return 0;

    /* contents of string tail undefined */

    s->len = new_sz;
    s->str[s->len] = 0;

    return 1;
}

int cstr_append_buf(cstring* s, const void* buf, size_t sz)
{
    if (!cstr_alloc_min_sz(s, s->len + sz))
        return 0;

    memcpy(s->str + s->len, buf, sz);
    s->len += sz;
    s->str[s->len] = 0;

    return 1;
}

int cstr_append_cstr(cstring* s, cstring* append)
{
    return cstr_append_buf(s, append->str, append->len);
}


int cstr_append_c(cstring* s, char ch)
{
    return cstr_append_buf(s, &ch, 1);
}


int cstr_equal(const cstring* a, const cstring* b)
{
    if (a == b)
        return 1;
    if (!a || !b)
        return 0;
    if (a->len != b->len)
        return 0;
    return (memcmp(a->str, b->str, a->len) == 0);
}

int cstr_compare(const cstring* a, const cstring* b)
{
    unsigned int i;
    if (a->len > b->len)
        return (1);
    if (a->len < b->len)
        return (-1);

    /* length equal, byte per byte compare */
    for (i = 0; i < a->len; i++) {
        char a1 = a->str[i];
        char b1 = b->str[i];

        if (a1 > b1)
            return (1);
        if (a1 < b1)
            return (-1);
    }
    return (0);
}

int cstr_erase(cstring* s, size_t pos, ssize_t len)
{
    ssize_t old_tail;

    if (pos == s->len && len == 0)
        return 1;
    if (pos >= s->len)
        return 0;

    old_tail = s->len - pos;
    if ((len >= 0) && (len > old_tail))
        return 0;

    memmove(&s->str[pos], &s->str[pos + len], old_tail - len);
    s->len -= len;
    s->str[s->len] = 0;

    return 1;
}
