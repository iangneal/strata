#ifndef _UTIL_H_
#define _UTIL_H_

#include <stdint.h>

#include "global/global.h"

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

#ifdef __GNUC__
#define TYPEOF(x) (__typeof__(x))
#else
#define TYPEOF(x)
#endif

#if defined(__i386__)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned long long int x;
	__asm__ volatile (".byte 0x0f, 0x31" : "=A" (x));
	return x;
}

static inline unsigned long long asm_rdtscp(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"ecx");
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );

}
#elif defined(__x86_64__)

static inline unsigned long long asm_rdtsc(void)
{
	unsigned hi, lo;
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

static inline unsigned long long asm_rdtscp(void)
{
	unsigned hi, lo;
	//__asm__ __volatile__ ("rdtscp" : "=a"(lo), "=d"(hi)::"rcx");
	__asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
	return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}
#else
#error "Only support for X86 architecture"
#endif

#define IS_ALIGNED(x, a) (((x) & ((__typeof__(x))(a) - 1)) == 0)

#ifdef __cplusplus
#define ALIGN(x, a)  ALIGN_MASK((x), ((__typeof__(x))(a) - 1))
#else
#define ALIGN(x, a)  ALIGN_MASK((x), ((typeof(x))(a) - 1))
#endif
#define ALIGN_MASK(x, mask)	(((x) + (mask)) & ~(mask))

static inline unsigned long ALIGN_FLOOR(unsigned long x, int mask)
{
	if (IS_ALIGNED(x, mask))
		return x;
	else
		return ALIGN(x, mask) - mask;
}

int is_power_of_two(unsigned long x);
unsigned int get_rand_interval(unsigned int min, unsigned int max);

float get_cpu_clock_speed(void);

// for directory path management

static inline int collapse_name(const char *input, char *_output)
{
	char *output = _output;

	while(1) {
		/* Detect a . or .. component */
		if (input[0] == '.') {
			if (input[1] == '.' && input[2] == '/') {
				/* A .. component */
				if (output == _output)
					return -1;
				input += 2;
				while (*(++input) == '/');
				while(--output != _output && *(output - 1) != '/');
				continue;
			} else if (input[1] == '/') {
				/* A . component */
				input += 1;
				while (*(++input) == '/');
				continue;
			}
		}

		/* Copy from here up until the first char of the next component */
		while(1) {
			*output++ = *input++;
			if (*input == '/') {
				*output++ = '/';
				/* Consume any extraneous separators */
				while (*(++input) == '/');
				break;
			} else if (*input == 0) {
				*output = 0;
				return output - _output;
			}
		}
	}
}

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   get_next_name("a/bb/c", name) = "bb/c", setting name = "a"
//   get_next_name("///a//bb", name) = "bb", setting name = "a"
//   get_next_name("a", name) = "", setting name = "a"
//   get_next_name("", name) = get_next_name("////", name) = 0
//
static inline const char* get_next_name(const char *path, char *name)
{
	const char *s;
	int len;

	while (*path == '/')
		path++;
	if (*path == 0)
		return 0;
	s = path;
	while (*path != '/' && *path != 0)
		path++;
	len = path - s;
	if (len >= DIRSIZ)
		memmove(name, s, DIRSIZ);
	else {
		memmove(name, s, len);
		name[len] = 0;
	}
	while (*path == '/')
		path++;
	return path;
}

/* /mlfs/aa/bb/c -> /mlfs/aa/bb (parent path) and c (name) */
static inline char* get_parent_path(const char *path, char *parent_path, char *name)
{
	int len = strlen(path);
	const char *_path = path;

	path += (len - 1);

	while (*path == '0') {
		path--;
		len--;
	}

	while (*path == '/') {
		path--;
		len--;
	}

	while (*path != '/' && len > 0) {
		len--;
		path--;
	}

	if (len == 0)
		return NULL;

	while (*path == '/') {
		len--;
		path--;
	}

	memmove(parent_path, _path, len);
	get_next_name(&_path[len], name);

	parent_path[len] = '\0';

	return parent_path;
}
#define tri_ratio(tsc,nr) tsc, nr, (nr ? (double)tsc/nr : 0.0) 
#define js_add_int64(obj, name, val) json_object_object_add(obj, name, json_object_new_int64(val));
#define js_add_double(obj, name, val) json_object_object_add(obj, name, json_object_new_double(val));
// statistics utils
typedef struct {
    uint64_t cnt;
    uint64_t total;
    uint64_t min;
    uint64_t max;
} stats_dist_t;
static inline void reset_stats_dist(stats_dist_t *s) {
    s->cnt = 0;
    s->total = 0;
    s->min = UINT64_MAX;
    s->max = 0;
}
static inline void update_stats_dist(stats_dist_t *s, uint64_t newval) {
    s->cnt++;
    s->total += newval;
    if (newval < s->min)
        s->min = newval;
    if (newval > s->max)
        s->max = newval;
}
static inline void print_stats_dist(stats_dist_t *s, const char *name) {
    printf("%s  : avg %.2f total %lu cnt %lu min %lu max %lu\n", name, (double)s->total/s->cnt, s->total, s->cnt, s->min, s->max);
}

void flush_llc(void);
#endif
