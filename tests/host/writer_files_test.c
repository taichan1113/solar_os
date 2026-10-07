#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "solar_os_writer_files.h"
#include "solar_os_writer_recovery.h"

int __real_rename(const char *src, const char *dst);

/* ESP-IDF FAT rename rejects existing targets. Exercise repeated checkpoints
 * and preservation of the old file under this contract, not POSIX overwrite. */
int __wrap_rename(const char *src, const char *dst)
{
    struct stat info;
    if (stat(dst, &info) == 0) {
        errno = EEXIST;
        return -1;
    }
    return __real_rename(src, dst);
}

static void write_plain(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static void expect_plain(const char *path, const char *text)
{
    char data[128];
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    const size_t len = fread(data, 1, sizeof(data), file);
    assert(fclose(file) == 0);
    assert(len == strlen(text));
    assert(memcmp(data, text, len) == 0);
}

int main(void)
{
    char temp[] = "/tmp/solaros-writer-test-XXXXXX";
    assert(mkdtemp(temp) != NULL);
    char path[160];
    assert(snprintf(path, sizeof(path), "%s/document.md", temp) > 0);

    static const char old_text[] = "old complete document\n";
    static const char new_text[] = "new complete document with more bytes\n";
    for (solar_os_writer_file_fault_t fault = SOLAR_OS_WRITER_FILE_FAULT_OPEN;
         fault <= SOLAR_OS_WRITER_FILE_FAULT_VERIFY;
         fault++) {
        write_plain(path, old_text);
        assert(solar_os_writer_safe_replace(path,
                                            new_text,
                                            sizeof(new_text) - 1U,
                                            fault) != ESP_OK);
        expect_plain(path, old_text);
    }

    assert(solar_os_writer_safe_replace(path,
                                        new_text,
                                        sizeof(new_text) - 1U,
                                        SOLAR_OS_WRITER_FILE_FAULT_NONE) == ESP_OK);
    expect_plain(path, new_text);

    assert(solar_os_writer_safe_replace(path, old_text, sizeof(old_text) - 1U,
                                        SOLAR_OS_WRITER_FILE_FAULT_NONE) == ESP_OK);
    expect_plain(path, old_text);
    assert(solar_os_writer_safe_replace(path, new_text, sizeof(new_text) - 1U,
                                        SOLAR_OS_WRITER_FILE_FAULT_NONE) == ESP_OK);
    expect_plain(path, new_text);

    assert(!solar_os_writer_recovery_due(false, false, 1999, 0, 0, 2000));
    assert(solar_os_writer_recovery_due(false, false, 2000, 0, 0, 2000));
    assert(!solar_os_writer_recovery_due(true, false, 9000, 0, 0, 2000));
    assert(!solar_os_writer_recovery_due(false, true, 3000, 0, 2000, 2000));
    assert(solar_os_writer_recovery_due(false, true, 4000, 0, 2000, 2000));
    assert(!solar_os_writer_recovery_due(false, false, 4000, 3900, 2000, 2000));
    assert(solar_os_writer_recovery_due(false, true, 1500, UINT32_MAX - 2500,
                                       UINT32_MAX - 1000, 2000));

    assert(unlink(path) == 0);
    assert(rmdir(temp) == 0);
    puts("writer FAT replacement/recovery retry tests: ok");
    return 0;
}

