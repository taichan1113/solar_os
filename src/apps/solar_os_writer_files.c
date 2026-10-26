#include "solar_os_writer_files.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "solar_os_storage.h"
#include "esp_log.h"

static esp_err_t writer_file_error(const char *phase, int error, esp_err_t result)
{
    const int saved = error != 0 ? error : EIO;
    ESP_LOGE("writer", "save phase=%s errno=%d result=0x%x", phase, saved, (unsigned)result);
    (void)phase;
    errno = saved;
    return result;
}

static bool writer_file_suffix(const char *path,
                               const char *suffix,
                               char *out,
                               size_t out_len)
{
    const int written = snprintf(out, out_len, "%s%s", path, suffix);
    return written >= 0 && (size_t)written < out_len;
}

static bool writer_file_matches(const char *path, const char *data, size_t len)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }
    char chunk[512];
    size_t offset = 0;
    bool matches = true;
    while (offset < len) {
        size_t take = len - offset;
        if (take > sizeof(chunk)) {
            take = sizeof(chunk);
        }
        if (fread(chunk, 1, take, file) != take ||
            memcmp(chunk, &data[offset], take) != 0) {
            matches = false;
            break;
        }
        offset += take;
    }
    if (matches && (fgetc(file) != EOF || ferror(file))) {
        matches = false;
    }
    if (fclose(file) != 0) {
        matches = false;
    }
    return matches;
}

esp_err_t solar_os_writer_safe_replace(const char *path,
                                       const char *data,
                                       size_t len,
                                       solar_os_writer_file_fault_t fault)
{
    return solar_os_writer_safe_replace_observed(path,data,len,fault,NULL,NULL);
}

esp_err_t solar_os_writer_safe_replace_observed(const char *path, const char *data,
    size_t len, solar_os_writer_file_fault_t fault,
    solar_os_writer_file_observer_t observer, void *context)
{
    if (path == NULL || path[0] == '\0' || (data == NULL && len > 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    char staged[SOLAR_OS_STORAGE_PATH_MAX];
    char backup[SOLAR_OS_STORAGE_PATH_MAX];
    if (!writer_file_suffix(path, ".writer.tmp", staged, sizeof(staged)) ||
        !writer_file_suffix(path, ".writer.bak", backup, sizeof(backup))) {
        return ESP_ERR_INVALID_SIZE;
    }

    (void)remove(staged);
    errno = 0;
    if (fault == SOLAR_OS_WRITER_FILE_FAULT_OPEN) {
        return writer_file_error("open", EIO, ESP_FAIL);
    }
    if (observer) observer(context,"CHECKPOINT_WRITE_START");
    FILE *file = fopen(staged, "wb");
    if (file == NULL) {
        return writer_file_error("open", errno, ESP_FAIL);
    }
    esp_err_t ret = ESP_OK;
    const char *phase = "write";
    if (fault == SOLAR_OS_WRITER_FILE_FAULT_SHORT_WRITE) {
        if (len) (void)fwrite(data,1,len-1,file);
        ret=ESP_FAIL;
    } else if (fault == SOLAR_OS_WRITER_FILE_FAULT_WRITE ||
        (len > 0 && fwrite(data, 1, len, file) != len)) {
        ret = ESP_FAIL;
    }
    if (ret == ESP_OK) {
        phase = "flush";
        if (fault == SOLAR_OS_WRITER_FILE_FAULT_FLUSH || fflush(file) != 0) ret = ESP_FAIL;
    }
    if (ret == ESP_OK) {
        phase = "fsync";
        if (fsync(fileno(file)) != 0) ret = ESP_FAIL;
    }
    int saved_error = ret != ESP_OK ? (errno != 0 ? errno : EIO) : 0;
    if (fclose(file) != 0 && ret == ESP_OK) {
        ret = ESP_FAIL;
        phase = "close";
        saved_error = errno;
    }
    if (observer) observer(context,"CHECKPOINT_WRITE_END");
    if (ret != ESP_OK) {
        (void)remove(staged);
        return writer_file_error(phase, saved_error, ret);
    }

    if (observer) observer(context,"CHECKPOINT_REPLACE_START");
    struct stat st;
    const bool had_original = stat(path, &st) == 0;
    (void)remove(backup);
    if (had_original) {
        if (fault == SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME ||
            rename(path, backup) != 0) {
            const int error = fault == SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME ? EIO : errno;
            (void)remove(staged);
            return writer_file_error("backup-rename", error, ESP_FAIL);
        }
    } else if (fault == SOLAR_OS_WRITER_FILE_FAULT_BACKUP_RENAME) {
        (void)remove(staged);
        return writer_file_error("backup-rename", EIO, ESP_FAIL);
    }

    if (fault == SOLAR_OS_WRITER_FILE_FAULT_FINAL_RENAME || rename(staged, path) != 0) {
        const int error = fault == SOLAR_OS_WRITER_FILE_FAULT_FINAL_RENAME ? EIO : errno;
        if (had_original) {
            (void)rename(backup, path);
        }
        (void)remove(staged);
        return writer_file_error("final-rename", error, ESP_FAIL);
    }

    if (fault == SOLAR_OS_WRITER_FILE_FAULT_VERIFY ||
        !writer_file_matches(path, data != NULL ? data : "", len)) {
        (void)remove(path);
        if (had_original) {
            (void)rename(backup, path);
        }
        return writer_file_error("verify", EIO, ESP_ERR_INVALID_STATE);
    }
    if (had_original) {
        (void)remove(backup);
    }
    if (observer) observer(context,"CHECKPOINT_REPLACE_END");
    return ESP_OK;
}

