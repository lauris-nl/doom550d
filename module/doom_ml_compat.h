#ifndef DOOM_ML_COMPAT_H
#define DOOM_ML_COMPAT_H

/*
 * Desktop-libc -> Magic Lantern compatibility declarations.
 *
 * Include standard headers first.  Afterwards the Doom source calls are
 * redirected to functions implemented in doom_ml_compat.c.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

void *doom_ml_malloc(size_t size);
void doom_ml_free(void *ptr);

void *doom_ml_fopen(const char *path, const char *mode);
int doom_ml_fclose(void *stream);
size_t doom_ml_fread(void *ptr, size_t size, size_t count, void *stream);
size_t doom_ml_fwrite(const void *ptr, size_t size, size_t count, void *stream);
int doom_ml_fseek(void *stream, long offset, int whence);
long doom_ml_ftell(void *stream);
int doom_ml_fflush(void *stream);
int doom_ml_fputs(const char *text, void *stream);
int doom_ml_fprintf(void *stream, const char *format, ...);
int doom_ml_vfprintf(void *stream, const char *format, va_list args);
int doom_ml_putchar(int character);

int doom_ml_remove(const char *path);
int doom_ml_rename(const char *old_path, const char *new_path);
int doom_ml_mkdir(const char *path, ...);
int doom_ml_system(const char *command);

double doom_ml_atof(const char *text);
int doom_ml_atoi(const char *text);
int doom_ml_sscanf(const char *text, const char *format, ...);
int doom_ml_strcasecmp(const char *left, const char *right);
int doom_ml_strncasecmp(const char *left, const char *right, size_t count);

int doom_ml_isalnum(int c);
int doom_ml_isalpha(int c);
int doom_ml_iscntrl(int c);
int doom_ml_isdigit(int c);
int doom_ml_isgraph(int c);
int doom_ml_islower(int c);
int doom_ml_isprint(int c);
int doom_ml_ispunct(int c);
int doom_ml_isspace(int c);
int doom_ml_isupper(int c);
int doom_ml_isxdigit(int c);
int doom_ml_tolower(int c);
int doom_ml_toupper(int c);

void doom_ml_exit(int status);
extern volatile int doom_ml_exit_requested;
extern volatile int doom_ml_last_exit_code;

/* Avoid newlib's stdout/stderr objects and therefore _impure_ptr. */
#ifdef stdin
#undef stdin
#endif
#ifdef stdout
#undef stdout
#endif
#ifdef stderr
#undef stderr
#endif
#define stdin  ((void *)0)
#define stdout ((void *)0)
#define stderr ((void *)0)

#undef malloc
#undef free
#define malloc doom_ml_malloc
#define free doom_ml_free

#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef fseek
#undef ftell
#undef fflush
#undef fputs
#undef fprintf
#undef vfprintf
#undef putchar
#define fopen doom_ml_fopen
#define fclose doom_ml_fclose
#define fread doom_ml_fread
#define fwrite doom_ml_fwrite
#define fseek doom_ml_fseek
#define ftell doom_ml_ftell
#define fflush doom_ml_fflush
#define fputs doom_ml_fputs
#define fprintf doom_ml_fprintf
#define vfprintf doom_ml_vfprintf
#define putchar doom_ml_putchar

#undef remove
#undef rename
#undef mkdir
#undef system
#define remove doom_ml_remove
#define rename doom_ml_rename
#define mkdir doom_ml_mkdir
#define system doom_ml_system

#undef atof
#undef atoi
#undef sscanf
#undef strcasecmp
#undef strncasecmp
#undef exit
#define atof doom_ml_atof
#define atoi doom_ml_atoi
#define sscanf doom_ml_sscanf
#define strcasecmp doom_ml_strcasecmp
#define strncasecmp doom_ml_strncasecmp
#define exit doom_ml_exit

/*
 * Newlib's ctype macros use _ctype_.  Replace all common ctype operations
 * with small ASCII-only versions; Doom's data and configuration are ASCII.
 */
#undef isalnum
#undef isalpha
#undef iscntrl
#undef isdigit
#undef isgraph
#undef islower
#undef isprint
#undef ispunct
#undef isspace
#undef isupper
#undef isxdigit
#undef tolower
#undef toupper

#define isalnum(c)  doom_ml_isalnum((unsigned char)(c))
#define isalpha(c)  doom_ml_isalpha((unsigned char)(c))
#define iscntrl(c)  doom_ml_iscntrl((unsigned char)(c))
#define isdigit(c)  doom_ml_isdigit((unsigned char)(c))
#define isgraph(c)  doom_ml_isgraph((unsigned char)(c))
#define islower(c)  doom_ml_islower((unsigned char)(c))
#define isprint(c)  doom_ml_isprint((unsigned char)(c))
#define ispunct(c)  doom_ml_ispunct((unsigned char)(c))
#define isspace(c)  doom_ml_isspace((unsigned char)(c))
#define isupper(c)  doom_ml_isupper((unsigned char)(c))
#define isxdigit(c) doom_ml_isxdigit((unsigned char)(c))
#define tolower(c)  doom_ml_tolower((unsigned char)(c))
#define toupper(c)  doom_ml_toupper((unsigned char)(c))

#endif
