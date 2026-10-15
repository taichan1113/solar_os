#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "solar_os_shell_completion.h"
#include "solar_os_shell_parse.h"

static void assert_completion_parse(const char *input,
                                    size_t expected_count,
                                    bool expected_trailing_space,
                                    const char *expected_last,
                                    size_t expected_last_start)
{
    char tokens[8][64] = {{0}};
    size_t starts[8] = {0};
    size_t count = 0;
    bool trailing_space = false;

    assert(solar_os_shell_completion_parse(input,
                                           strlen(input),
                                           &tokens[0][0],
                                           sizeof(tokens[0]),
                                           8,
                                           starts,
                                           &count,
                                           &trailing_space));
    assert(count == expected_count);
    assert(trailing_space == expected_trailing_space);
    if (expected_count > 0U) {
        assert(strcmp(tokens[expected_count - 1U], expected_last) == 0);
        assert(starts[expected_count - 1U] == expected_last_start);
    }
}

static void assert_token_round_trip(const char *token, const char *expected_encoded)
{
    char encoded[128];
    assert(solar_os_shell_completion_encode_token(token, encoded, sizeof(encoded)));
    assert(strcmp(encoded, expected_encoded) == 0);

    char line[160];
    snprintf(line, sizeof(line), "cat %s", encoded);
    char *argv[4];
    const solar_os_shell_parse_result_t result = solar_os_shell_tokenize(line, argv, 4);
    assert(result.error == SOLAR_OS_SHELL_PARSE_OK);
    assert(result.argc == 2);
    assert(strcmp(argv[1], token) == 0);
}

typedef struct {
    char candidates[24][64];
    size_t count;
} mode_candidates_t;

static void capture_mode_candidate(const char *candidate, void *context)
{
    mode_candidates_t *matches = context;
    assert(matches->count < 24 && strlen(candidate) < sizeof(matches->candidates[0]));
    strcpy(matches->candidates[matches->count++], candidate);
}

static void test_display_mode_candidates(void)
{
    mode_candidates_t matches = {0};
    solar_os_shell_completion_emit_mode_values(
        "refresh=<auto,partial,full,cleanup,fastest,typing,hybrid> "
        "timing=<on,off> hybrid-idle-ms=<500..30000> "
        "fastest-path=<scalar,tile> typing-cleanup-updates=<0..4096>",
        capture_mode_candidate, &matches);
    static const char * const expected[] = {
        "refresh=auto", "refresh=partial", "refresh=full", "refresh=cleanup",
        "refresh=fastest", "refresh=typing", "refresh=hybrid", "timing=on",
        "timing=off", "hybrid-idle-ms=<500..30000>",
        "fastest-path=scalar", "fastest-path=tile",
        "typing-cleanup-updates=<0..4096>",
    };
    assert(matches.count == sizeof(expected) / sizeof(expected[0]));
    for (size_t i = 0; i < matches.count; i++) {
        assert(strcmp(matches.candidates[i], expected[i]) == 0);
        assert(strlen(matches.candidates[i]) < 56); /* 140% terminal width */
    }
    assert(solar_os_shell_completion_common_prefix(matches.candidates[0],
                                                   matches.candidates[1]) ==
           strlen("refresh="));
}

int main(void)
{
    test_display_mode_candidates();
    assert(solar_os_shell_completion_common_prefix("", "radio") == 0U);
    assert(solar_os_shell_completion_common_prefix("radio", "radio") == 5U);
    assert(solar_os_shell_completion_common_prefix("radio", "ramfs") == 2U);
    assert(solar_os_shell_completion_common_prefix("mount", "unmount") == 0U);
    char aggregate[] = "radio";
    aggregate[solar_os_shell_completion_common_prefix(aggregate, "radar")] = '\0';
    aggregate[solar_os_shell_completion_common_prefix(aggregate, "radius")] = '\0';
    assert(aggregate[0] == 'r' && aggregate[1] == 'a' &&
           aggregate[2] == 'd' && aggregate[3] == '\0');
    assert(solar_os_shell_completion_common_prefix(NULL, "value") == 0U);
    assert(solar_os_shell_completion_common_prefix("value", NULL) == 0U);
    assert(!solar_os_shell_completion_needs_trailing_space(NULL));
    assert(!solar_os_shell_completion_needs_trailing_space(""));
    assert(!solar_os_shell_completion_needs_trailing_space("gesture="));
    assert(!solar_os_shell_completion_needs_trailing_space("audio="));
    assert(!solar_os_shell_completion_needs_trailing_space("video="));
    assert(solar_os_shell_completion_needs_trailing_space("audio=mic0"));
    assert(solar_os_shell_completion_needs_trailing_space("video=camera0"));
    assert(solar_os_shell_completion_needs_trailing_space("gesture=flick"));

    assert_completion_parse("cat My\\ F", 2U, false, "My F", 4U);
    assert_completion_parse("cat \"My F", 2U, false, "My F", 4U);
    assert_completion_parse("cat \"My \"", 2U, false, "My ", 4U);
    assert_completion_parse("cat ", 1U, true, "cat", 0U);
    assert_completion_parse("cat \"My Folder/\"f", 2U, false, "My Folder/f", 4U);

    assert_token_round_trip("readme.txt", "readme.txt");
    assert_token_round_trip("My File.txt", "\"My File.txt\"");
    assert_token_round_trip("dir/My Folder/", "\"dir/My Folder/\"");
    assert_token_round_trip("a'b", "\"a'b\"");
    assert_token_round_trip("a\"b\\c", "\"a\\\"b\\\\c\"");
    assert_token_round_trip("|", "\"|\"");
    puts("shell completion tests: ok");
    return 0;
}
