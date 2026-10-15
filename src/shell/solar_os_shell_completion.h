#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t solar_os_shell_completion_common_prefix(const char *first,
                                               const char *second);
bool solar_os_shell_completion_needs_trailing_space(const char *match);

typedef void (*solar_os_shell_completion_emitter_t)(const char *candidate,
                                                    void *context);
void solar_os_shell_completion_emit_mode_values(
    const char *values,
    solar_os_shell_completion_emitter_t emit,
    void *context);

bool solar_os_shell_completion_parse(const char *input,
                                     size_t input_len,
                                     char *tokens,
                                     size_t token_size,
                                     size_t token_capacity,
                                     size_t *starts,
                                     size_t *count,
                                     bool *trailing_space);

bool solar_os_shell_completion_encode_token(const char *token,
                                            char *encoded,
                                            size_t encoded_len);

#ifdef __cplusplus
}
#endif
