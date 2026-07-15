#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>

#include <module.h>
#include <dryos.h>
#include <fio-ml.h>

#define DOOM_ML_IO_CHUNK (64 * 1024)
#define DOOM_ML_FORMAT_BUFFER 768

typedef struct
{
    FILE *handle;
    uint32_t position;
    uint32_t size;
    int writable;
} doom_ml_file_t;

volatile int doom_ml_exit_requested = 0;
volatile int doom_ml_last_exit_code = 0;

static uint8_t *doom_ml_io_buffer = 0;

static void doom_ml_append_log(const char *text)
{
    FILE *file;
    size_t length;

    if (!text)
        return;

    file = FIO_OpenFile("ML/LOGS/DOOM550D.LOG", O_RDWR | O_SYNC);

    if (!file)
        file = FIO_CreateFile("ML/LOGS/DOOM550D.LOG");

    if (!file)
        return;

    length = strlen(text);
    FIO_SeekSkipFile(file, 0, SEEK_END);
    FIO_WriteFile(file, text, length);
    FIO_CloseFile(file);
}


static uint8_t *doom_ml_get_io_buffer(void)
{
    if (!doom_ml_io_buffer)
        doom_ml_io_buffer = fio_malloc(DOOM_ML_IO_CHUNK);

    return doom_ml_io_buffer;
}

void *doom_ml_malloc(size_t size)
{
    if (!size)
        size = 1;

    return __mem_malloc(size, 0, __FILE__, __LINE__);
}

void doom_ml_free(void *ptr)
{
    if (ptr)
        __mem_free(ptr);
}

static int doom_ml_mode_contains(const char *mode, char character)
{
    if (!mode)
        return 0;

    while (*mode)
    {
        if (*mode == character)
            return 1;

        mode++;
    }

    return 0;
}

void *doom_ml_fopen(const char *path, const char *mode)
{
    doom_ml_file_t *file;
    FILE *handle = 0;
    uint32_t file_size = 0;
    int writing;
    int appending;
    int updating;

    if (!path || !mode)
        return 0;

    writing = doom_ml_mode_contains(mode, 'w');
    appending = doom_ml_mode_contains(mode, 'a');
    updating = doom_ml_mode_contains(mode, '+');

    if (writing)
    {
        FIO_RemoveFile(path);
        handle = FIO_CreateFile(path);
    }
    else if (appending)
    {
        handle = FIO_OpenFile(path, O_RDWR | O_SYNC);

        if (!handle)
            handle = FIO_CreateFile(path);
    }
    else
    {
        handle = FIO_OpenFile(
            path,
            (updating ? O_RDWR : O_RDONLY) | O_SYNC
        );
    }

    if (!handle)
        return 0;

    file = doom_ml_malloc(sizeof(*file));

    if (!file)
    {
        FIO_CloseFile(handle);
        return 0;
    }

    memset(file, 0, sizeof(*file));
    file->handle = handle;
    file->writable = writing || appending || updating;

    if (FIO_GetFileSize(path, &file_size) == 0)
        file->size = file_size;

    if (appending)
    {
        FIO_SeekSkipFile(handle, 0, SEEK_END);
        file->position = file->size;
    }

    return file;
}

int doom_ml_fclose(void *stream)
{
    doom_ml_file_t *file = stream;

    if (!file)
        return -1;

    if (file->handle)
        FIO_CloseFile(file->handle);

    doom_ml_free(file);
    return 0;
}

size_t doom_ml_fread(
    void *ptr,
    size_t size,
    size_t count,
    void *stream
)
{
    doom_ml_file_t *file = stream;
    uint8_t *destination = ptr;
    uint8_t *temporary;
    size_t requested;
    size_t completed = 0;

    if (!file || !file->handle || !ptr || !size || !count)
        return 0;

    if (count > ((size_t)-1) / size)
        return 0;

    requested = size * count;
    temporary = doom_ml_get_io_buffer();

    if (!temporary)
        return 0;

    while (completed < requested)
    {
        size_t remaining = requested - completed;
        size_t chunk = remaining > DOOM_ML_IO_CHUNK
            ? DOOM_ML_IO_CHUNK
            : remaining;
        int received = FIO_ReadFile(file->handle, temporary, chunk);

        if (received <= 0)
            break;

        memcpy(destination + completed, temporary, received);
        completed += received;
        file->position += received;

        if ((size_t)received < chunk)
            break;
    }

    return completed / size;
}

size_t doom_ml_fwrite(
    const void *ptr,
    size_t size,
    size_t count,
    void *stream
)
{
    doom_ml_file_t *file = stream;
    const uint8_t *source = ptr;
    size_t requested;
    size_t completed = 0;

    if (!file || !file->handle || !ptr || !size || !count || !file->writable)
        return 0;

    if (count > ((size_t)-1) / size)
        return 0;

    requested = size * count;

    while (completed < requested)
    {
        size_t remaining = requested - completed;
        size_t chunk = remaining > DOOM_ML_IO_CHUNK
            ? DOOM_ML_IO_CHUNK
            : remaining;
        int written = FIO_WriteFile(file->handle, source + completed, chunk);

        if (written <= 0)
            break;

        completed += written;
        file->position += written;

        if (file->position > file->size)
            file->size = file->position;

        if ((size_t)written < chunk)
            break;
    }

    return completed / size;
}

int doom_ml_fseek(void *stream, long offset, int whence)
{
    doom_ml_file_t *file = stream;
    long new_position;

    if (!file || !file->handle)
        return -1;

    if (whence == SEEK_SET)
        new_position = offset;
    else if (whence == SEEK_CUR)
        new_position = (long)file->position + offset;
    else if (whence == SEEK_END)
        new_position = (long)file->size + offset;
    else
        return -1;

    if (new_position < 0)
        return -1;

    FIO_SeekSkipFile(file->handle, offset, whence);
    file->position = (uint32_t)new_position;
    return 0;
}

long doom_ml_ftell(void *stream)
{
    doom_ml_file_t *file = stream;

    if (!file)
        return -1;

    return (long)file->position;
}

int doom_ml_fflush(void *stream)
{
    (void)stream;
    return 0;
}

static int doom_ml_write_formatted(
    void *stream,
    const char *format,
    va_list args
)
{
    char buffer[DOOM_ML_FORMAT_BUFFER];
    int length;

    length = vsnprintf(buffer, sizeof(buffer), format, args);

    if (length < 0)
        return length;

    buffer[sizeof(buffer) - 1] = '\0';

    if (!stream)
    {
        doom_ml_append_log("stdio: ");
        doom_ml_append_log(buffer);
        printf("%s", buffer);
        return length;
    }

    {
        size_t actual = strlen(buffer);
        doom_ml_fwrite(buffer, 1, actual, stream);
    }

    return length;
}

int doom_ml_vfprintf(void *stream, const char *format, va_list args)
{
    return doom_ml_write_formatted(stream, format, args);
}

int doom_ml_fprintf(void *stream, const char *format, ...)
{
    va_list args;
    int result;

    va_start(args, format);
    result = doom_ml_write_formatted(stream, format, args);
    va_end(args);

    return result;
}

int doom_ml_fputs(const char *text, void *stream)
{
    size_t length;

    if (!text)
        return -1;

    length = strlen(text);

    if (!stream)
    {
        doom_ml_append_log("stdio: ");
        doom_ml_append_log(text);
        printf("%s", text);
        return (int)length;
    }

    return doom_ml_fwrite(text, 1, length, stream) == length
        ? (int)length
        : -1;
}

int doom_ml_putchar(int character)
{
    printf("%c", character);
    return character;
}

int doom_ml_remove(const char *path)
{
    if (!path)
        return -1;

    return FIO_RemoveFile(path);
}

int doom_ml_rename(const char *old_path, const char *new_path)
{
    doom_ml_file_t *source;
    doom_ml_file_t *destination;
    uint8_t *buffer;
    size_t received;
    int result = -1;

    if (!old_path || !new_path)
        return -1;

    source = doom_ml_fopen(old_path, "rb");

    if (!source)
        return -1;

    destination = doom_ml_fopen(new_path, "wb");

    if (!destination)
    {
        doom_ml_fclose(source);
        return -1;
    }

    buffer = doom_ml_get_io_buffer();

    if (!buffer)
        goto cleanup;

    result = 0;

    while ((received = doom_ml_fread(
        buffer,
        1,
        DOOM_ML_IO_CHUNK,
        source
    )) > 0)
    {
        if (doom_ml_fwrite(buffer, 1, received, destination) != received)
        {
            result = -1;
            break;
        }
    }

cleanup:
    doom_ml_fclose(destination);
    doom_ml_fclose(source);

    if (result == 0)
        FIO_RemoveFile(old_path);

    return result;
}

int doom_ml_mkdir(const char *path, ...)
{
    /*
     * Initial port: the required ML/DOOM directory is created on the card
     * by the deployment script.  Doom may request extra desktop-style
     * directories; treating those as already present is sufficient to boot.
     */
    (void)path;
    return 0;
}

int doom_ml_system(const char *command)
{
    (void)command;
    return -1;
}

static int doom_ml_ascii_lower(int c)
{
    if (c >= 'A' && c <= 'Z')
        return c + ('a' - 'A');

    return c;
}

int doom_ml_strncasecmp(
    const char *left,
    const char *right,
    size_t count
)
{
    while (count--)
    {
        int a = doom_ml_ascii_lower((unsigned char)*left++);
        int b = doom_ml_ascii_lower((unsigned char)*right++);

        if (a != b)
            return a - b;

        if (!a)
            return 0;
    }

    return 0;
}

int doom_ml_strcasecmp(const char *left, const char *right)
{
    while (*left || *right)
    {
        int a = doom_ml_ascii_lower((unsigned char)*left++);
        int b = doom_ml_ascii_lower((unsigned char)*right++);

        if (a != b)
            return a - b;
    }

    return 0;
}

static const char *doom_ml_skip_space(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
        text++;

    return text;
}

static long doom_ml_parse_integer(
    const char *text,
    int base,
    int *parsed
)
{
    int negative = 0;
    long value = 0;
    int digits = 0;

    text = doom_ml_skip_space(text);

    if (*text == '+' || *text == '-')
    {
        negative = *text == '-';
        text++;
    }

    if (base == 0)
    {
        if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        {
            base = 16;
            text += 2;
        }
        else if (text[0] == '0')
        {
            base = 8;
            text++;
            digits = 1;
        }
        else
        {
            base = 10;
        }
    }
    else if (
        base == 16 &&
        text[0] == '0' &&
        (text[1] == 'x' || text[1] == 'X')
    )
    {
        text += 2;
    }

    while (*text)
    {
        int digit;

        if (*text >= '0' && *text <= '9')
            digit = *text - '0';
        else if (*text >= 'a' && *text <= 'f')
            digit = *text - 'a' + 10;
        else if (*text >= 'A' && *text <= 'F')
            digit = *text - 'A' + 10;
        else
            break;

        if (digit >= base)
            break;

        value = value * base + digit;
        digits++;
        text++;
    }

    *parsed = digits > 0;
    return negative ? -value : value;
}

int doom_ml_atoi(const char *text)
{
    int parsed;
    return (int)doom_ml_parse_integer(text, 10, &parsed);
}

double doom_ml_atof(const char *text)
{
    int negative = 0;
    double value = 0.0;
    double fraction = 0.1;
    int exponent = 0;
    int exponent_negative = 0;

    text = doom_ml_skip_space(text);

    if (*text == '+' || *text == '-')
    {
        negative = *text == '-';
        text++;
    }

    while (*text >= '0' && *text <= '9')
    {
        value = value * 10.0 + (*text - '0');
        text++;
    }

    if (*text == '.')
    {
        text++;

        while (*text >= '0' && *text <= '9')
        {
            value += (*text - '0') * fraction;
            fraction *= 0.1;
            text++;
        }
    }

    if (*text == 'e' || *text == 'E')
    {
        text++;

        if (*text == '+' || *text == '-')
        {
            exponent_negative = *text == '-';
            text++;
        }

        while (*text >= '0' && *text <= '9')
        {
            exponent = exponent * 10 + (*text - '0');
            text++;
        }

        while (exponent-- > 0)
            value = exponent_negative ? value / 10.0 : value * 10.0;
    }

    return negative ? -value : value;
}

int doom_ml_sscanf(const char *text, const char *format, ...)
{
    va_list args;
    int base = 10;
    int parsed = 0;
    long value;

    if (!text || !format)
        return 0;

    if (strstr(format, "%x") || strstr(format, "%X"))
        base = 16;
    else if (strstr(format, "%o"))
        base = 8;
    else if (strstr(format, "%d"))
        base = 0;
    else if (!strstr(format, "%d"))
        return 0;

    value = doom_ml_parse_integer(text, base, &parsed);

    if (!parsed)
        return 0;

    va_start(args, format);

    if (
        strstr(format, "%x") ||
        strstr(format, "%X") ||
        strstr(format, "%o")
    )
    {
        unsigned int *result = va_arg(args, unsigned int *);
        *result = (unsigned int)value;
    }
    else
    {
        int *result = va_arg(args, int *);
        *result = (int)value;
    }

    va_end(args);
    return 1;
}

int doom_ml_isdigit(int c)
{
    return c >= '0' && c <= '9';
}

int doom_ml_isupper(int c)
{
    return c >= 'A' && c <= 'Z';
}

int doom_ml_islower(int c)
{
    return c >= 'a' && c <= 'z';
}

int doom_ml_isalpha(int c)
{
    return doom_ml_isupper(c) || doom_ml_islower(c);
}

int doom_ml_isalnum(int c)
{
    return doom_ml_isalpha(c) || doom_ml_isdigit(c);
}

int doom_ml_isspace(int c)
{
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}

int doom_ml_iscntrl(int c)
{
    return (c >= 0 && c < 32) || c == 127;
}

int doom_ml_isprint(int c)
{
    return c >= 32 && c <= 126;
}

int doom_ml_isgraph(int c)
{
    return c >= 33 && c <= 126;
}

int doom_ml_isxdigit(int c)
{
    return doom_ml_isdigit(c) ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int doom_ml_ispunct(int c)
{
    return doom_ml_isgraph(c) && !doom_ml_isalnum(c);
}

int doom_ml_tolower(int c)
{
    return doom_ml_isupper(c) ? c + ('a' - 'A') : c;
}

int doom_ml_toupper(int c)
{
    return doom_ml_islower(c) ? c - ('a' - 'A') : c;
}

void doom_ml_exit(int status)
{
    char message[80];

    doom_ml_last_exit_code = status;
    doom_ml_exit_requested = 1;

    snprintf(
        message,
        sizeof(message),
        "doom_ml_exit status=%d\n",
        status
    );

    doom_ml_append_log(message);
    printf("doom550d: Doom requested exit (%d)\n", status);
}
