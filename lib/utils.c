/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#include <config.h>

#include "utils.h"

#ifdef WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <Rpc.h>
    #include <shlobj.h>
    #include <psapi.h>
#else
    #include <arpa/inet.h>
#endif

#ifndef WIN32
#include <pwd.h>
#include <uuid/uuid.h>
#endif

#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>

#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>


#include <glib.h>
#include <glib/gstdio.h>
#include <rpcsyncwerk-utils.h>

#include <event2/util.h>

extern int inet_pton(int af, const char *src, void *dst);


struct timeval
timeval_from_msec (uint64_t milliseconds)
{
    struct timeval ret;
    const uint64_t microseconds = milliseconds * 1000;
    ret.tv_sec  = microseconds / 1000000;
    ret.tv_usec = microseconds % 1000000;
    return ret;
}

void
rawdata_to_hex (const unsigned char *rawdata, char *hex_str, int n_bytes)
{
    static const char hex[] = "0123456789abcdef";
    int i;

    for (i = 0; i < n_bytes; i++) {
        unsigned int val = *rawdata++;
        *hex_str++ = hex[val >> 4];
        *hex_str++ = hex[val & 0xf];
    }
    *hex_str = '\0';
}

static unsigned hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return ~0;
}

int
hex_to_rawdata (const char *hex_str, unsigned char *rawdata, int n_bytes)
{
    int i;
    for (i = 0; i < n_bytes; i++) {
        unsigned int val = (hexval(hex_str[0]) << 4) | hexval(hex_str[1]);
        if (val & ~0xff)
            return -1;
        *rawdata++ = val;
        hex_str += 2;
    }
    return 0;
}

size_t
ccnet_strlcpy (char *dest, const char *src, size_t size)
{
    size_t ret = strlen(src);

    if (size) {
        size_t len = (ret >= size) ? size - 1 : ret;
        memcpy(dest, src, len);
        dest[len] = '\0';
    }
    return ret;
}


int
checkdir (const char *dir)
{
    struct stat st;

#ifdef WIN32
    /* remove trailing '\\' */
    char *path = g_strdup(dir);
    char *p = (char *)path + strlen(path) - 1;
    while (*p == '\\' || *p == '/') *p-- = '\0';
    if ((g_stat(dir, &st) < 0) || !S_ISDIR(st.st_mode)) {
        g_free (path);
        return -1;
    }
    g_free (path);
    return 0;
#else
    if ((g_stat(dir, &st) < 0) || !S_ISDIR(st.st_mode))
        return -1;
    return 0;
#endif
}

int
checkdir_with_mkdir (const char *dir)
{
#ifdef WIN32
    int ret;
    char *path = g_strdup(dir);
    char *p = (char *)path + strlen(path) - 1;
    while (*p == '\\' || *p == '/') *p-- = '\0';
    ret = g_mkdir_with_parents(path, 0755);
    g_free (path);
    return ret;
#else
    return g_mkdir_with_parents(dir, 0755);
#endif
}

int
objstore_mkdir (const char *base)
{
    int ret;
    int i, j, len;
    static const char hex[] = "0123456789abcdef";
    char subdir[PATH_MAX];

    if ( (ret = checkdir_with_mkdir(base)) < 0)
        return ret;

    len = strlen(base);
    memcpy(subdir, base, len);
    subdir[len] = G_DIR_SEPARATOR;
    subdir[len+3] = '\0';

    for (i = 0; i < 16; i++) {
        subdir[len+1] = hex[i];
        for (j = 0; j < 16; j++) {
            subdir[len+2] = hex[j];
            if ( (ret = checkdir_with_mkdir(subdir)) < 0)
                return ret;
        }
    }
    return 0;
}

void
objstore_get_path (char *path, const char *base, const char *obj_id)
{
    int len;

    len = strlen(base);
    memcpy(path, base, len);
    path[len] = G_DIR_SEPARATOR;
    path[len+1] = obj_id[0];
    path[len+2] = obj_id[1];
    path[len+3] = G_DIR_SEPARATOR;
    strcpy(path+len+4, obj_id+2);
}


ssize_t						/* Read "n" bytes from a descriptor. */
readn(int fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nread = read(fd, ptr, nleft)) < 0) {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

ssize_t						/* Write "n" bytes to a descriptor. */
writen(int fd, const void *vptr, size_t n)
{
	size_t		nleft;
	ssize_t		nwritten;
	const char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
		if ( (nwritten = write(fd, ptr, nleft)) <= 0) {
			if (nwritten < 0 && errno == EINTR)
				nwritten = 0;		/* and call write() again */
			else
				return(-1);			/* error */
		}

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}


ssize_t						/* Read "n" bytes from a descriptor. */
recvn(evutil_socket_t fd, void *vptr, size_t n)
{
	size_t	nleft;
	ssize_t	nread;
	char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
#ifndef WIN32
        if ( (nread = read(fd, ptr, nleft)) < 0)
#else
        if ( (nread = recv(fd, ptr, nleft, 0)) < 0)
#endif
        {
			if (errno == EINTR)
				nread = 0;		/* and call read() again */
			else
				return(-1);
		} else if (nread == 0)
			break;				/* EOF */

		nleft -= nread;
		ptr   += nread;
	}
	return(n - nleft);		/* return >= 0 */
}

ssize_t						/* Write "n" bytes to a descriptor. */
sendn(evutil_socket_t fd, const void *vptr, size_t n)
{
	size_t		nleft;
	ssize_t		nwritten;
	const char	*ptr;

	ptr = vptr;
	nleft = n;
	while (nleft > 0) {
#ifndef WIN32
        if ( (nwritten = write(fd, ptr, nleft)) <= 0)
#else
        if ( (nwritten = send(fd, ptr, nleft, 0)) <= 0)
#endif
        {
			if (nwritten < 0 && errno == EINTR)
				nwritten = 0;		/* and call write() again */
			else
				return(-1);			/* error */
		}

		nleft -= nwritten;
		ptr   += nwritten;
	}
	return(n);
}

int copy_fd (int ifd, int ofd)
{
    while (1) {
        char buffer[8192];
        ssize_t len = readn (ifd, buffer, sizeof(buffer));
        if (!len)
            break;
        if (len < 0) {
            close (ifd);
            return -1;
        }
        if (writen (ofd, buffer, len) < 0) {
            close (ofd);
            return -1;
        }
    }
    close(ifd);
    return 0;
}

int copy_file (const char *dst, const char *src, int mode)
{
    int fdi, fdo, status;

    if ((fdi = g_open (src, O_RDONLY | O_BINARY, 0)) < 0)
        return fdi;

    fdo = g_open (dst, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, mode);
    if (fdo < 0 && errno == EEXIST) {
        close (fdi);
        return 0;
    } else if (fdo < 0){
        close (fdi);
        return -1;
    }

    status = copy_fd (fdi, fdo);
    if (close (fdo) != 0)
        return -1;

    return status;
}

char*
ccnet_expand_path (const char *src)
{
#ifdef WIN32
    char new_path[PATH_MAX + 1];
    char *p = new_path;
    const char *q = src;

    memset(new_path, 0, sizeof(new_path));
    if (*src == '~') {
        const char *home = g_get_home_dir();
        memcpy(new_path, home, strlen(home));
        p += strlen(new_path);
        q++;
    }
    memcpy(p, q, strlen(q));

    /* delete the charactor '\' or '/' at the end of the path
     * because the function stat faied to deal with directory names
     * with '\' or '/' in the end */
    p = new_path + strlen(new_path) - 1;
    while(*p == '\\' || *p == '/') *p-- = '\0';

    return strdup (new_path);
#else
    const char *next_in, *ntoken;
    char new_path[PATH_MAX + 1];
    char *next_out;
    int len;

   /* special cases */
    if (!src || *src == '\0')
        return NULL;
    if (strlen(src) > PATH_MAX)
        return NULL;

    next_in = src;
    next_out = new_path;
    *next_out = '\0';

    if (*src == '~') {
        /* handle src start with '~' or '~<user>' like '~plt' */
        struct passwd *pw = NULL;

        for ( ; *next_in != '/' && *next_in != '\0'; next_in++) ;

        len = next_in - src;
        if (len == 1) {
            pw = getpwuid (geteuid());
        } else {
            /* copy '~<user>' to new_path */
            memcpy (new_path, src, len);
            new_path[len] = '\0';
            pw = getpwnam (new_path + 1);
        }
        if (pw == NULL)
            return NULL;

        len = strlen (pw->pw_dir);
        memcpy (new_path, pw->pw_dir, len);
        next_out = new_path + len;
        *next_out = '\0';

        if (*next_in == '\0')
            return strdup (new_path);
    } else if (*src != '/') {
        getcwd (new_path, PATH_MAX);
        for ( ; *next_out; next_out++) ; /* to '\0' */
    }

    while (*next_in != '\0') {
        /* move ntoken to the next not '/' char  */
        for (ntoken = next_in; *ntoken == '/'; ntoken++) ;

        for (next_in = ntoken; *next_in != '/'
                 && *next_in != '\0'; next_in++) ;

        len = next_in - ntoken;

        if (len == 0) {
            /* the path ends with '/', keep it */
            *next_out++ = '/';
            *next_out = '\0';
            break;
        }

        if (len == 2 && ntoken[0] == '.' && ntoken[1] == '.')
        {
            /* '..' */
            for (; next_out > new_path && *next_out != '/'; next_out--)
                ;
            *next_out = '\0';
        } else if (ntoken[0] != '.' || len != 1) {
            /* not '.' */
            *next_out++ = '/';
            memcpy (next_out, ntoken, len);
            next_out += len;
            *next_out = '\0';
        }
    }

    /* the final special case */
    if (new_path[0] == '\0') {
        new_path[0] = '/';
        new_path[1] = '\0';
    }
    return strdup (new_path);
#endif
}


int
calculate_sha1 (unsigned char *sha1, const char *msg)
{
    SHA_CTX c;

    SHA1_Init(&c);
    SHA1_Update(&c, msg, strlen(msg));
	SHA1_Final(sha1, &c);
    return 0;
}

uint32_t
ccnet_sha1_hash (const void *v)
{
    /* 31 bit hash function */
    const unsigned char *p = v;
    uint32_t h = 0;
    int i;

    for (i = 0; i < 20; i++)
        h = (h << 5) - h + p[i];

    return h;
}

int
ccnet_sha1_equal (const void *v1,
                  const void *v2)
{
    const unsigned char *p1 = v1;
    const unsigned char *p2 = v2;
    int i;

    for (i = 0; i < 20; i++)
        if (p1[i] != p2[i])
            return 0;

    return 1;
}

#ifndef WIN32
char* gen_uuid ()
{
    char *uuid_str = g_malloc (37);
    uuid_t uuid;

    uuid_generate (uuid);
    uuid_unparse_lower (uuid, uuid_str);

    return uuid_str;
}

void gen_uuid_inplace (char *buf)
{
    uuid_t uuid;

    uuid_generate (uuid);
    uuid_unparse_lower (uuid, buf);
}

gboolean
is_uuid_valid (const char *uuid_str)
{
    uuid_t uuid;

    if (uuid_parse (uuid_str, uuid) < 0)
        return FALSE;
    return TRUE;
}

#else
char* gen_uuid ()
{
    char *uuid_str = g_malloc (37);
    unsigned char *str = NULL;
    UUID uuid;

    UuidCreate(&uuid);
    UuidToString(&uuid, &str);
    memcpy(uuid_str, str, 37);
    RpcStringFree(&str);
    return uuid_str;
}

void gen_uuid_inplace (char *buf)
{
    unsigned char *str = NULL;
    UUID uuid;

    UuidCreate(&uuid);
    UuidToString(&uuid, &str);
    memcpy(buf, str, 37);
    RpcStringFree(&str);
}

gboolean
is_uuid_valid (const char *uuid_str)
{
    UUID uuid;
    if (UuidFromString((unsigned char *)uuid_str, &uuid) != RPC_S_OK)
        return FALSE;
    return TRUE;
}

#endif

char** strsplit_by_space (char *string, int *length)
{
    char *remainder, *s;
    int size = 8, num = 0, done = 0;
    char **array;

    if (string == NULL || string[0] == '\0') {
        *length = 0;
        return NULL;
    }

    array = malloc (sizeof(char *) * size);

    remainder = string;
    while (!done) {
        for (s = remainder; *s != ' ' && *s != '\0'; ++s) ;

        if (*s == '\0')
            done = 1;
        else
            *s = '\0';

        array[num++] = remainder;
        if (!done && num == size) {
            size <<= 1;
            array = realloc (array, sizeof(char *) * size);
        }

        remainder = s + 1;
    }

    *length = num;
    return array;
}

char** strsplit_by_char (char *string, int *length, char c)
{
    char *remainder, *s;
    int size = 8, num = 0, done = 0;
    char **array;

    if (string == NULL || string[0] == '\0') {
        *length = 0;
        return NULL;
    }

    array = malloc (sizeof(char *) * size);

    remainder = string;
    while (!done) {
        for (s = remainder; *s != c && *s != '\0'; ++s) ;

        if (*s == '\0')
            done = 1;
        else
            *s = '\0';

        array[num++] = remainder;
        if (!done && num == size) {
            size <<= 1;
            array = realloc (array, sizeof(char *) * size);
        }

        remainder = s + 1;
    }

    *length = num;
    return array;
}

char* strjoin_n (const char *seperator, int argc, char **argv)
{
    GString *buf;
    int i;
    char *str;

    if (argc == 0)
        return NULL;

    buf = g_string_new (argv[0]);
    for (i = 1; i < argc; ++i) {
        g_string_append (buf, seperator);
        g_string_append (buf, argv[i]);
    }

    str = buf->str;
    g_string_free (buf, FALSE);
    return str;
}


gboolean is_ipaddr_valid (const char *ip)
{
    unsigned char buf[sizeof(struct in6_addr)];

    if (evutil_inet_pton(AF_INET, ip, buf) == 1)
        return TRUE;

    if (evutil_inet_pton(AF_INET6, ip, buf) == 1)
        return TRUE;

    return FALSE;
}

void parse_key_value_pairs (char *string, KeyValueFunc func, void *data)
{
    char *line = string, *next, *space;
    char *key, *value;

    while (*line) {
        /* handle empty line */
        if (*line == '\n') {
            ++line;
            continue;
        }

        for (next = line; *next != '\n' && *next; ++next) ;
        *next = '\0';

        for (space = line; space < next && *space != ' '; ++space) ;
        if (*space != ' ') {
            g_warning ("Bad key value format: %s\n", line);
            return;
        }
        *space = '\0';
        key = line;
        value = space + 1;

        func (data, key, value);

        line = next + 1;
    }
}

void parse_key_value_pairs2 (char *string, KeyValueFunc2 func, void *data)
{
    char *line = string, *next, *space;
    char *key, *value;

    while (*line) {
        /* handle empty line */
        if (*line == '\n') {
            ++line;
            continue;
        }

        for (next = line; *next != '\n' && *next; ++next) ;
        *next = '\0';

        for (space = line; space < next && *space != ' '; ++space) ;
        if (*space != ' ') {
            g_warning ("Bad key value format: %s\n", line);
            return;
        }
        *space = '\0';
        key = line;
        value = space + 1;

        if (func(data, key, value) == FALSE)
            break;

        line = next + 1;
    }
}

/**
 * handle the empty string problem.
 */
gchar*
ccnet_key_file_get_string (GKeyFile *keyf,
                           const char *category,
                           const char *key)
{
    gchar *v;

    if (!g_key_file_has_key (keyf, category, key, NULL))
        return NULL;

    v = g_key_file_get_string (keyf, category, key, NULL);
    if (v != NULL && v[0] == '\0') {
        g_free(v);
        return NULL;
    }

    return g_strchomp(v);
}

/**
 * string_list_is_exists:
 * @str_list:
 * @string: a C string or %NULL
 *
 * Check whether @string is in @str_list.
 *
 * returns: %TRUE if @string is in str_list, %FALSE otherwise
 */
gboolean
string_list_is_exists (GList *str_list, const char *string)
{
    GList *ptr;
    for (ptr = str_list; ptr; ptr = ptr->next) {
        if (g_strcmp0(string, ptr->data) == 0)
            return TRUE;
    }
    return FALSE;
}

/**
 * string_list_append:
 * @str_list:
 * @string: a C string (can't be %NULL
 *
 * Append @string to @str_list if it is in the list.
 *
 * returns: the new start of the list
 */
GList*
string_list_append (GList *str_list, const char *string)
{
    g_return_val_if_fail (string != NULL, str_list);

    if (string_list_is_exists(str_list, string))
        return str_list;

    str_list = g_list_append (str_list, g_strdup(string));
    return str_list;
}

GList *
string_list_append_sorted (GList *str_list, const char *string)
{
    g_return_val_if_fail (string != NULL, str_list);

    if (string_list_is_exists(str_list, string))
        return str_list;

    str_list = g_list_insert_sorted_with_data (str_list, g_strdup(string),
                                 (GCompareDataFunc)g_strcmp0, NULL);
    return str_list;
}


GList *
string_list_remove (GList *str_list, const char *string)
{
    g_return_val_if_fail (string != NULL, str_list);

    GList *ptr;

    for (ptr = str_list; ptr; ptr = ptr->next) {
        if (strcmp((char *)ptr->data, string) == 0) {
            g_free (ptr->data);
            return g_list_delete_link (str_list, ptr);
        }
    }
    return str_list;
}


void
string_list_free (GList *str_list)
{
    GList *ptr = str_list;

    while (ptr) {
        g_free (ptr->data);
        ptr = ptr->next;
    }

    g_list_free (str_list);
}


void
string_list_join (GList *str_list, GString *str, const char *seperator)
{
    GList *ptr;
    if (!str_list)
        return;

    ptr = str_list;
    g_string_append (str, ptr->data);

    for (ptr = ptr->next; ptr; ptr = ptr->next) {
        g_string_append (str, seperator);
        g_string_append (str, (char *)ptr->data);
    }
}

GList *
string_list_parse (const char *list_in_str, const char *seperator)
{
    if (!list_in_str)
        return NULL;

    GList *list = NULL;
    char **array = g_strsplit (list_in_str, seperator, 0);
    char **ptr;

    for (ptr = array; *ptr; ptr++) {
        list = g_list_prepend (list, g_strdup(*ptr));
    }
    list = g_list_reverse (list);

    g_strfreev (array);
    return list;
}

GList *
string_list_parse_sorted (const char *list_in_str, const char *seperator)
{
    GList *list = string_list_parse (list_in_str, seperator);

    return g_list_sort (list, (GCompareFunc)g_strcmp0);
}

gboolean
string_list_sorted_is_equal (GList *list1, GList *list2)
{
    GList *ptr1 = list1, *ptr2 = list2;

    while (ptr1 && ptr2) {
        if (g_strcmp0(ptr1->data, ptr2->data) != 0)
            break;

        ptr1 = ptr1->next;
        ptr2 = ptr2->next;
    }

    if (!ptr1 && !ptr2)
        return TRUE;
    return FALSE;
}

char **
ncopy_string_array (char **orig, int n)
{
    char **ret = g_malloc (sizeof(char *) * n);
    int i = 0;

    for (; i < n; i++)
        ret[i] = g_strdup(orig[i]);
    return ret;
}

void
nfree_string_array (char **array, int n)
{
    int i = 0;

    for (; i < n; i++)
        g_free (array[i]);
    g_free (array);
}

gint64
get_current_time()
{
    GTimeVal tv;
    gint64 t;

    g_get_current_time (&tv);
    t = tv.tv_sec * (gint64)1000000 + tv.tv_usec;
    return t;
}

#ifdef WIN32
int
pgpipe (ccnet_pipe_t handles[2])
{
    SOCKET s;
    struct sockaddr_in serv_addr;
    int len = sizeof( serv_addr );

    handles[0] = handles[1] = INVALID_SOCKET;

    if ( ( s = socket( AF_INET, SOCK_STREAM, 0 ) ) == INVALID_SOCKET )
    {
        g_debug("pgpipe failed to create socket: %d\n", WSAGetLastError());
        return -1;
    }

    memset( &serv_addr, 0, sizeof( serv_addr ) );
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(0);
    serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    BOOL reuse = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (bind(s, (SOCKADDR *) & serv_addr, len) == SOCKET_ERROR)
    {
        g_debug("pgpipe failed to bind: %d\n", WSAGetLastError());
        closesocket(s);
        return -1;
    }
    if (listen(s, 1) == SOCKET_ERROR)
    {
        g_debug("pgpipe failed to listen: %d\n", WSAGetLastError());
        closesocket(s);
        return -1;
    }
    if (getsockname(s, (SOCKADDR *) & serv_addr, &len) == SOCKET_ERROR)
    {
        g_debug("pgpipe failed to getsockname: %d\n", WSAGetLastError());
        closesocket(s);
        return -1;
    }
    if ((handles[1] = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
    {
        g_debug("pgpipe failed to create socket 2: %d\n", WSAGetLastError());
        closesocket(s);
        return -1;
    }

    if (connect(handles[1], (SOCKADDR *) & serv_addr, len) == SOCKET_ERROR)
    {
        g_debug("pgpipe failed to connect socket: %d\n", WSAGetLastError());
        closesocket(s);
        return -1;
    }
    if ((handles[0] = accept(s, (SOCKADDR *) & serv_addr, &len)) == INVALID_SOCKET)
    {
        g_debug("pgpipe failed to accept socket: %d\n", WSAGetLastError());
        closesocket(handles[1]);
        handles[1] = INVALID_SOCKET;
        closesocket(s);
        return -1;
    }
    closesocket(s);
    return 0;
}
#endif

/*
  The EVP_EncryptXXX and EVP_DecryptXXX series of functions have a
  weird choice of returned value.
*/
#define ENC_SUCCESS 1
#define ENC_FAILURE 0
#define DEC_SUCCESS 1
#define DEC_FAILURE 0


#include <openssl/aes.h>
#include <openssl/evp.h>

/* Block size, in bytes. For AES it can only be 16 bytes. */
#define BLK_SIZE 16
#define ENCRYPT_BLK_SIZE BLK_SIZE
#define KEY_SIZE 32

int
ccnet_generate_cipher (const char *passwd, int plen,
                       unsigned char *key, unsigned char *iv)
{
    int key_len;

    /* Generate the derived key. We use AES 256 bits key,
       CBC cipher mode, and SHA1 as the message digest
       when generating the key. IV is not used in ecb mode,
       actually. */
    key_len  = EVP_BytesToKey (EVP_aes_256_cbc(), /* cipher mode */
                               EVP_sha1(),        /* message digest */
                               NULL,              /* salt */
                               (unsigned char*)passwd,
                               plen,
                               3,   /* iteration times */
                               key, /* the derived key */
                               iv); /* IV, initial vector */

    /* The key should be 32 bytes long for our 256 bit key. */
    if (key_len != KEY_SIZE) {
        g_warning ("failed to init EVP_CIPHER_CTX.\n");
        return -1;
    }

    return 0;
}

int
ccnet_encrypt_with_key (char **data_out,
                        int *out_len,
                        const char *data_in,
                        const int in_len,
                        const unsigned char *key,
                        const unsigned char *iv)
{
    *data_out = NULL;
    *out_len = -1;

    /* check validation */
    if (data_in == NULL || in_len <= 0 ||
         key == NULL || iv == NULL) {
        g_warning ("Invalid params.\n");
        return -1;
    }

    EVP_CIPHER_CTX *ctx;
    int ret;
    int blks;

    /* Prepare CTX for encryption. */
    ctx = EVP_CIPHER_CTX_new ();

    ret = EVP_EncryptInit_ex (ctx,
                              EVP_aes_256_cbc(), /* cipher mode */
                              NULL, /* engine, NULL for default */
                              key,  /* derived key */
                              iv);  /* initial vector */

    if (ret == ENC_FAILURE) {
        g_warning ("error init\n");
	EVP_CIPHER_CTX_free (ctx);
        return -1;
    }

    /* Allocating output buffer. */

    /*
      For EVP symmetric encryption, padding is always used __even if__
      data size is a multiple of block size, in which case the padding
      length is the block size. so we have the following:
    */

    blks = (in_len / BLK_SIZE) + 1;
    *data_out = (char *) g_malloc(blks * BLK_SIZE);
    if (*data_out == NULL) {
        g_warning ("failed to allocate the output buffer.\n");
        goto enc_error;
    }

    int update_len, final_len;

    /* Do the encryption. */
    ret = EVP_EncryptUpdate (ctx,
                             (unsigned char*)*data_out,
                             &update_len,
                             (unsigned char*)data_in,
                             in_len);
    if (ret == ENC_FAILURE) {
        g_warning ("error update\n");
        goto enc_error;
    }

    /* Finish the possible partial block. */
    ret = EVP_EncryptFinal_ex (ctx,
                               (unsigned char*)*data_out + update_len,
                               &final_len);
    *out_len = update_len + final_len;
    /* out_len should be equal to the allocated buffer size. */
    if (ret == ENC_FAILURE || *out_len != (blks * BLK_SIZE)) {
        goto enc_error;
    }

    EVP_CIPHER_CTX_free (ctx);
    return 0;

enc_error:
    EVP_CIPHER_CTX_free (ctx);
    *out_len = -1;
    if (*data_out != NULL)
        g_free (*data_out);
    *data_out = NULL;
    return -1;
}

int
ccnet_decrypt_with_key (char **data_out,
                        int *out_len,
                        const char *data_in,
                        const int in_len,
                        const unsigned char *key,
                        const unsigned char *iv)
{
    /* Check validation. Because padding is always used, in_len must
     * be a multiple of BLK_SIZE */
    if ( data_in == NULL || in_len <= 0 || in_len % BLK_SIZE != 0 ||
         key == NULL || iv == NULL) {

        g_warning ("Invalid param(s).\n");
        return -1;
    }

    EVP_CIPHER_CTX *ctx;
    int ret;

    *data_out = NULL;
    *out_len = -1;

    /* Prepare CTX for decryption. */
    ctx = EVP_CIPHER_CTX_new (); 
    ret = EVP_DecryptInit_ex (ctx,
                              EVP_aes_256_cbc(), /* cipher mode */
                              NULL, /* engine, NULL for default */
                              key,  /* derived key */
                              iv);  /* initial vector */

    if (ret == DEC_FAILURE) {
        EVP_CIPHER_CTX_free (ctx); 
        return -1;
    }
    /* Allocating output buffer. */
    *data_out = (char *)g_malloc (in_len);
    if (*data_out == NULL) {
        g_warning ("failed to allocate the output buffer.\n");
        goto dec_error;
    }

    int update_len, final_len;

    /* Do the decryption. */
    ret = EVP_DecryptUpdate (ctx,
                             (unsigned char*)*data_out,
                             &update_len,
                             (unsigned char*)data_in,
                             in_len);
    if (ret == DEC_FAILURE)
        goto dec_error;

    /* Finish the possible partial block. */
    ret = EVP_DecryptFinal_ex (ctx,
                               (unsigned char*)*data_out + update_len,
                               &final_len);
    *out_len = update_len + final_len;
    /* out_len should be smaller than in_len. */
    if (ret == DEC_FAILURE || *out_len > in_len)
        goto dec_error;

    EVP_CIPHER_CTX_free (ctx); 
    return 0;

dec_error:
    EVP_CIPHER_CTX_free (ctx); 
    *out_len = -1;
    if (*data_out != NULL)
        g_free (*data_out);
    *data_out = NULL;
    return -1;
}

/* convert locale specific input to utf8 encoded string  */
char *ccnet_locale_to_utf8 (const gchar *src)
{
    if (!src)
        return NULL;

    gsize bytes_read = 0;
    gsize bytes_written = 0;
    GError *error = NULL;
    gchar *dst = NULL;

    dst = g_locale_to_utf8
        (src,                   /* locale specific string */
         strlen(src),           /* len of src */
         &bytes_read,           /* length processed */
         &bytes_written,        /* output length */
         &error);

    if (error) {
        return NULL;
    }

    return dst;
}

/* convert utf8 input to locale specific string  */
char *ccnet_locale_from_utf8 (const gchar *src)
{
    if (!src)
        return NULL;

    gsize bytes_read = 0;
    gsize bytes_written = 0;
    GError *error = NULL;
    gchar *dst = NULL;

    dst = g_locale_from_utf8
        (src,                   /* locale specific string */
         strlen(src),           /* len of src */
         &bytes_read,           /* length processed */
         &bytes_written,        /* output length */
         &error);

    if (error) {
        return NULL;
    }

    return dst;
}

#ifdef WIN32

static HANDLE
get_process_handle (const char *process_name_in)
{
    char name[256];
    if (strstr(process_name_in, ".exe")) {
        snprintf (name, sizeof(name), "%s", process_name_in);
    } else {
        snprintf (name, sizeof(name), "%s.exe", process_name_in);
    }

    DWORD aProcesses[1024], cbNeeded, cProcesses;

    if (!EnumProcesses(aProcesses, sizeof(aProcesses), &cbNeeded))
        return NULL;

    /* Calculate how many process identifiers were returned. */
    cProcesses = cbNeeded / sizeof(DWORD);

    HANDLE hProcess;
    HMODULE hMod;
    char process_name[MAX_PATH];
    unsigned int i;

    for (i = 0; i < cProcesses; i++) {
        if(aProcesses[i] == 0)
            continue;
        hProcess = OpenProcess (PROCESS_ALL_ACCESS, FALSE, aProcesses[i]);
        if (!hProcess)
            continue;

        if (EnumProcessModules(hProcess, &hMod, sizeof(hMod), &cbNeeded)) {
            GetModuleBaseName(hProcess, hMod, process_name,
                              sizeof(process_name)/sizeof(char));
        }

        if (strcasecmp(process_name, name) == 0)
            return hProcess;
        else {
            CloseHandle(hProcess);
        }
    }
    /* Not found */
    return NULL;
}

gboolean
process_is_running (const char *process_name)
{
    HANDLE proc_handle = get_process_handle(process_name);

    if (proc_handle) {
        CloseHandle(proc_handle);
        return TRUE;
    } else {
        return FALSE;
    }
}

int
win32_kill_process (const char *process_name)
{
    HANDLE proc_handle = get_process_handle(process_name);

    if (proc_handle) {
        TerminateProcess(proc_handle, 0);
        CloseHandle(proc_handle);
        return 0;
    } else {
        return -1;
    }
}

int
win32_spawn_process (char *cmdline_in, char *working_directory)
{
    if (!cmdline_in)
        return -1;
    char *cmdline = g_strdup (cmdline_in);

    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    unsigned flags;
    BOOL success;

    /* we want to execute syncwerk without crreating a console window */
    flags = CREATE_NO_WINDOW;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_FORCEOFFFEEDBACK;
    si.hStdInput = (HANDLE) _get_osfhandle(0);
    si.hStdOutput = (HANDLE) _get_osfhandle(1);
    si.hStdError = (HANDLE) _get_osfhandle(2);

    memset(&pi, 0, sizeof(pi));

    success = CreateProcess(NULL, cmdline, NULL, NULL, TRUE, flags,
                            NULL, working_directory, &si, &pi);
    g_free (cmdline);
    if (!success) {
        g_warning ("failed to fork_process: GLE=%lu\n", GetLastError());
        return -1;
    }

    /* close the handle of thread so that the process object can be freed by
     * system
     */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}

char *
wchar_to_utf8 (const wchar_t *wch)
{
    if (wch == NULL) {
        return NULL;
    }

    char *utf8 = NULL;
    int bufsize, len;

    bufsize = WideCharToMultiByte
        (CP_UTF8,               /* multibyte code page */
         0,                     /* flags */
         wch,                   /* src */
         -1,                    /* src len, -1 for all includes \0 */
         utf8,                  /* dst */
         0,                     /* dst buf len */
         NULL,                  /* default char */
         NULL);                 /* BOOL flag indicates default char is used */

    if (bufsize <= 0) {
        g_warning ("failed to convert a string from wchar to utf8 0");
        return NULL;
    }

    utf8 = g_malloc(bufsize);
    len = WideCharToMultiByte
        (CP_UTF8,               /* multibyte code page */
         0,                     /* flags */
         wch,                   /* src */
         -1,                    /* src len, -1 for all includes \0 */
         utf8,                  /* dst */
         bufsize,               /* dst buf len */
         NULL,                  /* default char */
         NULL);                 /* BOOL flag indicates default char is used */

    if (len != bufsize) {
        g_free (utf8);
        g_warning ("failed to convert a string from wchar to utf8");
        return NULL;
    }

    return utf8;
}

wchar_t *
wchar_from_utf8 (const char *utf8)
{
    if (utf8 == NULL) {
        return NULL;
    }

    wchar_t *wch = NULL;
    int bufsize, len;

    bufsize = MultiByteToWideChar
        (CP_UTF8,               /* multibyte code page */
         0,                     /* flags */
         utf8,                  /* src */
         -1,                    /* src len, -1 for all includes \0 */
         wch,                   /* dst */
         0);                    /* dst buf len */

    if (bufsize <= 0) {
        g_warning ("failed to convert a string from wchar to utf8 0");
        return NULL;
    }

    wch = g_malloc (bufsize * sizeof(wchar_t));
    len = MultiByteToWideChar
        (CP_UTF8,               /* multibyte code page */
         0,                     /* flags */
         utf8,                  /* src */
         -1,                    /* src len, -1 for all includes \0 */
         wch,                   /* dst */
         bufsize);              /* dst buf len */

    if (len != bufsize) {
        g_free (wch);
        g_warning ("failed to convert a string from utf8 to wchar");
        return NULL;
    }

    return wch;
}

/* Get the commandline arguments in unicode, then convert them to utf8  */
char **
get_argv_utf8 (int *argc)
{
    int i = 0;
    char **argv = NULL;
    const wchar_t *cmdline = NULL;
    wchar_t **argv_w = NULL;

    cmdline = GetCommandLineW();
    argv_w = CommandLineToArgvW (cmdline, argc);
    if (!argv_w) {
        printf("failed to CommandLineToArgvW(), GLE=%lu\n", GetLastError());
        return NULL;
    }

    argv = (char **)malloc (sizeof(char*) * (*argc));
    for (i = 0; i < *argc; i++) {
        argv[i] = wchar_to_utf8 (argv_w[i]);
    }

    return argv;
}
#endif  /* ifdef WIN32 */

#ifdef __linux__
/* read the link of /proc/123/exe and compare with `process_name' */
static int
find_process_in_dirent(struct dirent *dir, const char *process_name)
{
    char path[512];
    /* fisrst construct a path like /proc/123/exe */
    if (sprintf (path, "/proc/%s/exe", dir->d_name) < 0) {
        return -1;
    }

    char buf[PATH_MAX];
    /* get the full path of exe */
    ssize_t l = readlink(path, buf, PATH_MAX);

    if (l < 0)
        return -1;
    buf[l] = '\0';

    /* get the base name of exe */
    char *base = g_path_get_basename(buf);
    int ret = strcmp(base, process_name);
    g_free(base);

    if (ret == 0)
        return atoi(dir->d_name);
    else
        return -1;
}

/* read the /proc fs to determine whether some process is running */
gboolean process_is_running (const char *process_name)
{
    DIR *proc_dir = opendir("/proc");
    if (!proc_dir) {
        fprintf (stderr, "failed to open /proc/ dir\n");
        return FALSE;
    }

    struct dirent *subdir = NULL;
    while ((subdir = readdir(proc_dir))) {
        char first = subdir->d_name[0];
        /* /proc/[1-9][0-9]* */
        if (first > '9' || first < '1')
            continue;
        int pid = find_process_in_dirent(subdir, process_name);
        if (pid > 0) {
            closedir(proc_dir);
            return TRUE;
        }
    }

    closedir(proc_dir);
    return FALSE;
}
#endif

#ifdef __APPLE__
gboolean process_is_running (const char *process_name)
{
    //TODO
    return FALSE;
}
#endif

char*
ccnet_object_type_from_id (const char *object_id)
{
    char *ptr;

    if ( !(ptr = strchr(object_id, '/')) )
        return NULL;

    return g_strndup(object_id, ptr - object_id);
}


#ifdef WIN32
/**
 * In Win32 we need to use _stat64 for files larger than 2GB. _stat64 needs
 * the `path' argument in gbk encoding.
 */
    #define STAT_STRUCT struct __stat64
    #define STAT_FUNC win_stat64_utf8

static inline int
win_stat64_utf8 (char *path_u8, STAT_STRUCT *sb)
{
    char *path = ccnet_locale_from_utf8(path_u8);
    int result = _stat64(path, sb);
    g_free (path);
    return result;
}

#else
    #define STAT_STRUCT struct stat
    #define STAT_FUNC stat
#endif

static gint64
calc_recursively (const char *path, GError **calc_error)
{
    gint64 sum = 0;

    GError *error = NULL;
    GDir *folder = g_dir_open(path, 0, &error);
    if (!folder) {
        g_set_error (calc_error, CCNET_DOMAIN, 0,
                     "g_open() dir %s failed:%s\n", path, error->message);
        return -1;
    }

    const char *name = NULL;
    while ((name = g_dir_read_name(folder)) != NULL) {
        STAT_STRUCT sb;
        char *full_path= g_build_filename (path, name, NULL);
        if (STAT_FUNC(full_path, &sb) < 0) {
            g_set_error (calc_error, CCNET_DOMAIN, 0, "failed to stat on %s: %s\n",
                         full_path, strerror(errno));
            g_free(full_path);
            g_dir_close(folder);
            return -1;
        }

        if (S_ISDIR(sb.st_mode)) {
            gint64 size = calc_recursively(full_path, calc_error);
            if (size < 0) {
                g_free (full_path);
                g_dir_close (folder);
                return -1;
            }
            sum += size;
            g_free(full_path);
        } else if (S_ISREG(sb.st_mode)) {
            sum += sb.st_size;
            g_free(full_path);
        }
    }

    g_dir_close (folder);
    return sum;
}

gint64
ccnet_calc_directory_size (const char *path, GError **error)
{
    return calc_recursively (path, error);
}


#ifdef WIN32
/*
 * strtok_r code directly from glibc.git /string/strtok_r.c since windows
 * doesn't have it.
 */
char *
strtok_r(char *s, const char *delim, char **save_ptr)
{
    char *token;
    
    if(s == NULL)
        s = *save_ptr;
    
    /* Scan leading delimiters.  */
    s += strspn(s, delim);
    if(*s == '\0') {
        *save_ptr = s;
        return NULL;
    }
    
    /* Find the end of the token.  */
    token = s;
    s = strpbrk(token, delim);
    
    if(s == NULL) {
        /* This token finishes the string.  */
        *save_ptr = strchr(token, '\0');
    } else {
        /* Terminate the token and make *SAVE_PTR point past it.  */
        *s = '\0';
        *save_ptr = s + 1;
    }
    
    return token;
}
#endif
