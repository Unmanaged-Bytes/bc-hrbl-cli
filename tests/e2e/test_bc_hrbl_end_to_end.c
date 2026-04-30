// SPDX-License-Identifier: MIT

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cmocka.h>

#ifndef BC_HRBL_TEST_BINARY_PATH
#error "BC_HRBL_TEST_BINARY_PATH must be defined"
#endif

typedef struct command_result {
    int exit_code;
    char* stdout_buffer;
    size_t stdout_length;
} command_result_t;

static void command_result_free(command_result_t* result)
{
    free(result->stdout_buffer);
    result->stdout_buffer = NULL;
    result->stdout_length = 0u;
}

static bool run_capture(char* const argv[], command_result_t* out_result)
{
    int pipe_fd[2];
    if (pipe(pipe_fd) != 0) {
        return false;
    }
    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        execv(argv[0], argv);
        _exit(127);
    }
    close(pipe_fd[1]);

    size_t capacity = 4096u;
    char* buffer = malloc(capacity);
    size_t length = 0u;
    for (;;) {
        if (length + 1024u > capacity) {
            capacity *= 2u;
            char* grown = realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                close(pipe_fd[0]);
                waitpid(pid, NULL, 0);
                return false;
            }
            buffer = grown;
        }
        ssize_t bytes_read = read(pipe_fd[0], buffer + length, capacity - length - 1u);
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            close(pipe_fd[0]);
            waitpid(pid, NULL, 0);
            return false;
        }
        if (bytes_read == 0) {
            break;
        }
        length += (size_t)bytes_read;
    }
    buffer[length] = '\0';
    close(pipe_fd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    out_result->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    out_result->stdout_buffer = buffer;
    out_result->stdout_length = length;
    return true;
}

static char* write_temp_file(const char* suffix, const void* content, size_t length)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "/tmp/bc-hrbl-testXXXXXX%s", suffix);
    size_t suffix_length = strlen(suffix);
    int fd = mkstemps(pattern, (int)suffix_length);
    if (fd < 0) {
        return NULL;
    }
    const unsigned char* bytes = (const unsigned char*)content;
    size_t written_total = 0u;
    while (written_total < length) {
        ssize_t chunk = write(fd, bytes + written_total, length - written_total);
        if (chunk <= 0) {
            close(fd);
            unlink(pattern);
            return NULL;
        }
        written_total += (size_t)chunk;
    }
    close(fd);
    return strdup(pattern);
}

static void test_version_exits_zero(void** state)
{
    (void)state;
    char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "--version", NULL};
    command_result_t result = {0, NULL, 0u};
    assert_true(run_capture(argv, &result));
    assert_int_equal(result.exit_code, 0);
    assert_non_null(strstr(result.stdout_buffer, "bc-hrbl"));
    command_result_free(&result);
}

static void test_help_lists_commands(void** state)
{
    (void)state;
    char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "--help", NULL};
    command_result_t result = {0, NULL, 0u};
    assert_true(run_capture(argv, &result));
    assert_int_equal(result.exit_code, 0);
    assert_non_null(strstr(result.stdout_buffer, "verify"));
    assert_non_null(strstr(result.stdout_buffer, "query"));
    assert_non_null(strstr(result.stdout_buffer, "inspect"));
    assert_non_null(strstr(result.stdout_buffer, "convert"));
    command_result_free(&result);
}

static void test_unknown_command_exits_two(void** state)
{
    (void)state;
    char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "unknown", NULL};
    command_result_t result = {0, NULL, 0u};
    assert_true(run_capture(argv, &result));
    assert_int_equal(result.exit_code, 2);
    command_result_free(&result);
}

static void test_convert_from_json_then_verify_then_query(void** state)
{
    (void)state;
    const char* json_body = "{\"server\":{\"host\":\"localhost\",\"port\":8080},\"ports\":[80,443,8080]}";
    char* json_path = write_temp_file(".json", json_body, strlen(json_body));
    assert_non_null(json_path);
    char* hrbl_path = write_temp_file(".hrbl", "", 0u);
    assert_non_null(hrbl_path);

    {
        char* const argv[] = {
            (char*)BC_HRBL_TEST_BINARY_PATH, "convert", "--from=json", json_path, "-o", hrbl_path, NULL,
        };
        command_result_t result = {0, NULL, 0u};
        assert_true(run_capture(argv, &result));
        assert_int_equal(result.exit_code, 0);
        command_result_free(&result);
    }
    {
        char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "verify", hrbl_path, NULL};
        command_result_t result = {0, NULL, 0u};
        assert_true(run_capture(argv, &result));
        assert_int_equal(result.exit_code, 0);
        assert_non_null(strstr(result.stdout_buffer, "ok"));
        command_result_free(&result);
    }
    {
        char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "query", hrbl_path, "server.port", NULL};
        command_result_t result = {0, NULL, 0u};
        assert_true(run_capture(argv, &result));
        assert_int_equal(result.exit_code, 0);
        assert_non_null(strstr(result.stdout_buffer, "8080"));
        command_result_free(&result);
    }
    {
        char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "query", hrbl_path, "ports[1]", NULL};
        command_result_t result = {0, NULL, 0u};
        assert_true(run_capture(argv, &result));
        assert_int_equal(result.exit_code, 0);
        assert_non_null(strstr(result.stdout_buffer, "443"));
        command_result_free(&result);
    }
    {
        char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "inspect", hrbl_path, NULL};
        command_result_t result = {0, NULL, 0u};
        assert_true(run_capture(argv, &result));
        assert_int_equal(result.exit_code, 0);
        assert_non_null(strstr(result.stdout_buffer, "\"host\": \"localhost\""));
        assert_non_null(strstr(result.stdout_buffer, "\"port\": 8080"));
        command_result_free(&result);
    }

    unlink(json_path);
    unlink(hrbl_path);
    free(json_path);
    free(hrbl_path);
}

static void test_verify_rejects_nonexistent_file(void** state)
{
    (void)state;
    char* const argv[] = {(char*)BC_HRBL_TEST_BINARY_PATH, "verify", "/tmp/bc-hrbl-does-not-exist-xyz", NULL};
    command_result_t result = {0, NULL, 0u};
    assert_true(run_capture(argv, &result));
    assert_int_not_equal(result.exit_code, 0);
    command_result_free(&result);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_version_exits_zero),
        cmocka_unit_test(test_help_lists_commands),
        cmocka_unit_test(test_unknown_command_exits_two),
        cmocka_unit_test(test_convert_from_json_then_verify_then_query),
        cmocka_unit_test(test_verify_rejects_nonexistent_file),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
