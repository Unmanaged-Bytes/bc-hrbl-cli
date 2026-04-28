// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"
#include "bc_allocators_pool.h"
#include "bc_core.h"
#include "bc_core_cpu.h"
#include "bc_core_format.h"
#include "bc_core_io.h"
#include "bc_core_memory.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef BC_HRBL_CLI_VERSION_STRING
#define BC_HRBL_CLI_VERSION_STRING "0.0.0-unversioned"
#endif

typedef bool (*bc_hrbl_export_function_t)(const bc_hrbl_reader_t*, bc_core_writer_t*);

static const char bc_hrbl_cli_usage_text[] = "bc-hrbl " BC_HRBL_CLI_VERSION_STRING "\n"
                                             "Usage: bc-hrbl <command> [options] [arguments]\n"
                                             "\n"
                                             "Commands:\n"
                                             "  verify <file>                        integrity check a .hrbl file\n"
                                             "  query <file> <path>                  lookup a dotted path\n"
                                             "  inspect <file> [-o OUT]              dump as pretty JSON\n"
                                             "  convert --from=json IN [-o OUT]      bootstrap .hrbl from JSON\n"
                                             "  convert --to=json IN [-o OUT]        export .hrbl to JSON\n"
                                             "  convert --to=yaml IN [-o OUT]        export .hrbl to YAML\n"
                                             "  convert --to=ini  IN [-o OUT]        export .hrbl to INI\n"
                                             "\n"
                                             "  -h, --help                           show this help and exit\n"
                                             "  -v, --version                        print version and exit\n";

static size_t bc_hrbl_cli_cstring_length(const char* value)
{
    size_t length = 0;
    (void)bc_core_length(value, '\0', &length);
    return length;
}

static bool bc_hrbl_cli_cstring_equal(const char* left, const char* right)
{
    size_t left_length = bc_hrbl_cli_cstring_length(left);
    size_t right_length = bc_hrbl_cli_cstring_length(right);
    if (left_length != right_length) {
        return false;
    }
    bool result = false;
    if (!bc_core_equal(left, right, left_length, &result)) {
        return false;
    }
    return result;
}

static void bc_hrbl_cli_emit_writer_message(int fd, const char* part1, const char* part2, const char* part3, const char* part4,
                                            const char* part5)
{
    char buffer[1024];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
        return;
    }
    if (part1 != NULL) {
        (void)bc_core_writer_write_cstring(&writer, part1);
    }
    if (part2 != NULL) {
        (void)bc_core_writer_write_cstring(&writer, part2);
    }
    if (part3 != NULL) {
        (void)bc_core_writer_write_cstring(&writer, part3);
    }
    if (part4 != NULL) {
        (void)bc_core_writer_write_cstring(&writer, part4);
    }
    if (part5 != NULL) {
        (void)bc_core_writer_write_cstring(&writer, part5);
    }
    (void)bc_core_writer_flush(&writer);
    (void)bc_core_writer_destroy(&writer);
}

static void bc_hrbl_cli_emit_stderr(const char* part1, const char* part2, const char* part3, const char* part4, const char* part5)
{
    bc_hrbl_cli_emit_writer_message(STDERR_FILENO, part1, part2, part3, part4, part5);
}

static void bc_hrbl_cli_emit_stdout(const char* part1, const char* part2, const char* part3)
{
    bc_hrbl_cli_emit_writer_message(STDOUT_FILENO, part1, part2, part3, NULL, NULL);
}

static int bc_hrbl_cli_print_usage_to_fd(int fd)
{
    char buffer[2048];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
        return 0;
    }
    (void)bc_core_writer_write_cstring(&writer, bc_hrbl_cli_usage_text);
    (void)bc_core_writer_flush(&writer);
    (void)bc_core_writer_destroy(&writer);
    return 0;
}

static bool bc_hrbl_cli_export_via_fd(int fd, const bc_hrbl_reader_t* reader, bc_hrbl_export_function_t export_fn)
{
    char writer_buffer[4096] BC_CACHE_LINE_ALIGNED;
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, fd, writer_buffer, sizeof(writer_buffer))) {
        return false;
    }
    bool ok = export_fn(reader, &writer);
    if (!bc_core_writer_flush(&writer)) {
        ok = false;
    }
    (void)bc_core_writer_destroy(&writer);
    return ok;
}

static bool bc_hrbl_cli_read_file_contents(bc_allocators_context_t* memory_context, const char* path, char** out_data, size_t* out_size)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct stat stat_info;
    if (fstat(fd, &stat_info) != 0) {
        (void)close(fd);
        return false;
    }
    if (stat_info.st_size < 0) {
        (void)close(fd);
        return false;
    }
    size_t size = (size_t)stat_info.st_size;
    void* pointer = NULL;
    if (!bc_allocators_pool_allocate(memory_context, size == 0u ? 1u : size, &pointer)) {
        (void)close(fd);
        return false;
    }
    char* data = (char*)pointer;
    if (size != 0u) {
        char reader_buffer[4096];
        bc_core_reader_t reader;
        if (!bc_core_reader_init(&reader, fd, reader_buffer, sizeof(reader_buffer))) {
            bc_allocators_pool_free(memory_context, data);
            (void)close(fd);
            return false;
        }
        if (!bc_core_reader_read_exact(&reader, data, size)) {
            (void)bc_core_reader_destroy(&reader);
            bc_allocators_pool_free(memory_context, data);
            (void)close(fd);
            return false;
        }
        (void)bc_core_reader_destroy(&reader);
    }
    (void)close(fd);
    *out_data = data;
    *out_size = size;
    return true;
}

static bool bc_hrbl_cli_write_file_contents(const char* path, const void* data, size_t size)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return false;
    }
    bool ok = true;
    if (size != 0u) {
        char writer_buffer[4096];
        bc_core_writer_t writer;
        if (!bc_core_writer_init(&writer, fd, writer_buffer, sizeof(writer_buffer))) {
            (void)close(fd);
            return false;
        }
        if (!bc_core_writer_write_bytes(&writer, data, size)) {
            ok = false;
        }
        if (!bc_core_writer_flush(&writer)) {
            ok = false;
        }
        (void)bc_core_writer_destroy(&writer);
    }
    if (close(fd) != 0) {
        ok = false;
    }
    return ok;
}

static bool bc_hrbl_cli_write_buffer_to_fd(int fd, const void* data, size_t size)
{
    char writer_buffer[4096];
    bc_core_writer_t writer;
    if (!bc_core_writer_init(&writer, fd, writer_buffer, sizeof(writer_buffer))) {
        return false;
    }
    bool ok = true;
    if (size != 0u && !bc_core_writer_write_bytes(&writer, data, size)) {
        ok = false;
    }
    if (!bc_core_writer_flush(&writer)) {
        ok = false;
    }
    (void)bc_core_writer_destroy(&writer);
    return ok;
}

static int bc_hrbl_cli_command_verify(int argument_count, char** argument_values)
{
    if (argument_count != 3) {
        bc_hrbl_cli_emit_stderr("bc-hrbl verify: expected exactly one file argument\n", NULL, NULL, NULL, NULL);
        return 2;
    }
    const char* path = argument_values[2];
    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status == BC_HRBL_VERIFY_OK) {
        bc_hrbl_cli_emit_stdout("ok ", path, "\n");
        return 0;
    }
    bc_hrbl_cli_emit_stderr("bc-hrbl verify: ", path, ": ", bc_hrbl_verify_status_name(status), "\n");
    return 1;
}

static bool bc_hrbl_cli_print_value_to_fd(int fd, const bc_hrbl_value_ref_t* value)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_value_kind(value, &kind)) {
        return false;
    }
    switch (kind) {
    case BC_HRBL_KIND_NULL:
        bc_hrbl_cli_emit_writer_message(fd, "null\n", NULL, NULL, NULL, NULL);
        return true;
    case BC_HRBL_KIND_FALSE:
        bc_hrbl_cli_emit_writer_message(fd, "false\n", NULL, NULL, NULL, NULL);
        return true;
    case BC_HRBL_KIND_TRUE:
        bc_hrbl_cli_emit_writer_message(fd, "true\n", NULL, NULL, NULL, NULL);
        return true;
    case BC_HRBL_KIND_INT64: {
        int64_t v = 0;
        if (!bc_hrbl_reader_get_int64(value, &v)) {
            return false;
        }
        char buffer[64];
        bc_core_writer_t writer;
        if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
            return false;
        }
        bool ok = bc_core_writer_write_signed_integer_64(&writer, v);
        ok = ok && bc_core_writer_write_char(&writer, '\n');
        ok = ok && bc_core_writer_flush(&writer);
        (void)bc_core_writer_destroy(&writer);
        return ok;
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t v = 0u;
        if (!bc_hrbl_reader_get_uint64(value, &v)) {
            return false;
        }
        char buffer[64];
        bc_core_writer_t writer;
        if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
            return false;
        }
        bool ok = bc_core_writer_write_unsigned_integer_64_decimal(&writer, v);
        ok = ok && bc_core_writer_write_char(&writer, '\n');
        ok = ok && bc_core_writer_flush(&writer);
        (void)bc_core_writer_destroy(&writer);
        return ok;
    }
    case BC_HRBL_KIND_FLOAT64: {
        double v = 0.0;
        if (!bc_hrbl_reader_get_float64(value, &v)) {
            return false;
        }
        char buffer[64];
        bc_core_writer_t writer;
        if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
            return false;
        }
        bool ok = bc_core_writer_write_double_shortest_round_trip(&writer, v);
        ok = ok && bc_core_writer_write_char(&writer, '\n');
        ok = ok && bc_core_writer_flush(&writer);
        (void)bc_core_writer_destroy(&writer);
        return ok;
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = NULL;
        size_t length = 0u;
        if (!bc_hrbl_reader_get_string(value, &data, &length)) {
            return false;
        }
        char buffer[4096];
        bc_core_writer_t writer;
        if (!bc_core_writer_init(&writer, fd, buffer, sizeof(buffer))) {
            return false;
        }
        bool ok = true;
        if (length != 0u) {
            ok = bc_core_writer_write_bytes(&writer, data, length);
        }
        ok = ok && bc_core_writer_write_char(&writer, '\n');
        ok = ok && bc_core_writer_flush(&writer);
        (void)bc_core_writer_destroy(&writer);
        return ok;
    }
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_cli_export_via_fd(fd, value->reader, bc_hrbl_export_json);
    }
    return false;
}

static int bc_hrbl_cli_command_query(int argument_count, char** argument_values)
{
    if (argument_count != 4) {
        bc_hrbl_cli_emit_stderr("bc-hrbl query: expected <file> <path> arguments\n", NULL, NULL, NULL, NULL);
        return 2;
    }
    const char* path = argument_values[2];
    const char* dotted = argument_values[3];
    size_t dotted_length = bc_hrbl_cli_cstring_length(dotted);

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status != BC_HRBL_VERIFY_OK) {
        bc_hrbl_cli_emit_stderr("bc-hrbl query: ", path, ": ", bc_hrbl_verify_status_name(status), "\n");
        return 1;
    }

    bc_allocators_context_config_t config;
    (void)bc_core_zero(&config, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        bc_hrbl_cli_emit_stderr("bc-hrbl query: allocator init failed\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, path, &reader)) {
        bc_allocators_context_destroy(memory);
        bc_hrbl_cli_emit_stderr("bc-hrbl query: cannot open '", path, "'\n", NULL, NULL);
        return 1;
    }
    bc_hrbl_value_ref_t value;
    if (!bc_hrbl_reader_find(reader, dotted, dotted_length, &value)) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        bc_hrbl_cli_emit_stderr("bc-hrbl query: path not found: ", dotted, "\n", NULL, NULL);
        return 1;
    }
    if (!bc_hrbl_cli_print_value_to_fd(STDOUT_FILENO, &value)) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        bc_hrbl_cli_emit_stderr("bc-hrbl query: cannot render value\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    bc_hrbl_reader_destroy(reader);
    bc_allocators_context_destroy(memory);
    return 0;
}

static int bc_hrbl_cli_command_inspect(int argument_count, char** argument_values)
{
    const char* path = NULL;
    const char* output_path = NULL;
    for (int i = 2; i < argument_count; i += 1) {
        const char* arg = argument_values[i];
        if (bc_hrbl_cli_cstring_equal(arg, "-o")) {
            if (i + 1 >= argument_count) {
                bc_hrbl_cli_emit_stderr("bc-hrbl inspect: -o requires a path\n", NULL, NULL, NULL, NULL);
                return 2;
            }
            output_path = argument_values[i + 1];
            i += 1;
            continue;
        }
        if (path == NULL) {
            path = arg;
            continue;
        }
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: unexpected argument '", arg, "'\n", NULL, NULL);
        return 2;
    }
    if (path == NULL) {
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: missing input file\n", NULL, NULL, NULL, NULL);
        return 2;
    }

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status != BC_HRBL_VERIFY_OK) {
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: ", path, ": ", bc_hrbl_verify_status_name(status), "\n");
        return 1;
    }

    bc_allocators_context_config_t config;
    (void)bc_core_zero(&config, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: allocator init failed\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, path, &reader)) {
        bc_allocators_context_destroy(memory);
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: cannot open '", path, "'\n", NULL, NULL);
        return 1;
    }

    int output_fd = STDOUT_FILENO;
    bool owns_output_fd = false;
    if (output_path != NULL) {
        output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (output_fd < 0) {
            bc_hrbl_reader_destroy(reader);
            bc_allocators_context_destroy(memory);
            bc_hrbl_cli_emit_stderr("bc-hrbl inspect: cannot open output '", output_path, "'\n", NULL, NULL);
            return 1;
        }
        owns_output_fd = true;
    }
    bool ok = bc_hrbl_cli_export_via_fd(output_fd, reader, bc_hrbl_export_json);
    if (owns_output_fd) {
        if (close(output_fd) != 0) {
            ok = false;
        }
    }
    bc_hrbl_reader_destroy(reader);
    bc_allocators_context_destroy(memory);
    if (!ok) {
        bc_hrbl_cli_emit_stderr("bc-hrbl inspect: export failed\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    return 0;
}

typedef enum bc_hrbl_cli_convert_direction {
    BC_HRBL_CLI_CONVERT_NONE = 0,
    BC_HRBL_CLI_CONVERT_FROM_JSON = 1,
    BC_HRBL_CLI_CONVERT_TO_JSON = 2,
    BC_HRBL_CLI_CONVERT_TO_YAML = 3,
    BC_HRBL_CLI_CONVERT_TO_INI = 4
} bc_hrbl_cli_convert_direction_t;

static int bc_hrbl_cli_command_convert(int argument_count, char** argument_values)
{
    bc_hrbl_cli_convert_direction_t direction = BC_HRBL_CLI_CONVERT_NONE;
    const char* input_path = NULL;
    const char* output_path = NULL;
    for (int i = 2; i < argument_count; i += 1) {
        const char* arg = argument_values[i];
        if (bc_hrbl_cli_cstring_equal(arg, "--from=json")) {
            direction = BC_HRBL_CLI_CONVERT_FROM_JSON;
            continue;
        }
        if (bc_hrbl_cli_cstring_equal(arg, "--to=json")) {
            direction = BC_HRBL_CLI_CONVERT_TO_JSON;
            continue;
        }
        if (bc_hrbl_cli_cstring_equal(arg, "--to=yaml")) {
            direction = BC_HRBL_CLI_CONVERT_TO_YAML;
            continue;
        }
        if (bc_hrbl_cli_cstring_equal(arg, "--to=ini")) {
            direction = BC_HRBL_CLI_CONVERT_TO_INI;
            continue;
        }
        if (bc_hrbl_cli_cstring_equal(arg, "-o")) {
            if (i + 1 >= argument_count) {
                bc_hrbl_cli_emit_stderr("bc-hrbl convert: -o requires a path\n", NULL, NULL, NULL, NULL);
                return 2;
            }
            output_path = argument_values[i + 1];
            i += 1;
            continue;
        }
        if (input_path == NULL) {
            input_path = arg;
            continue;
        }
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: unexpected argument '", arg, "'\n", NULL, NULL);
        return 2;
    }
    if (direction == BC_HRBL_CLI_CONVERT_NONE) {
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: specify --from=json or --to=json|yaml|ini\n", NULL, NULL, NULL, NULL);
        return 2;
    }
    if (input_path == NULL) {
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: missing input file\n", NULL, NULL, NULL, NULL);
        return 2;
    }

    if (direction == BC_HRBL_CLI_CONVERT_FROM_JSON) {
        bc_allocators_context_config_t config;
        (void)bc_core_zero(&config, sizeof(config));
        bc_allocators_context_t* memory = NULL;
        if (!bc_allocators_context_create(&config, &memory)) {
            bc_hrbl_cli_emit_stderr("bc-hrbl convert: allocator init failed\n", NULL, NULL, NULL, NULL);
            return 1;
        }
        char* json_data = NULL;
        size_t json_size = 0u;
        if (!bc_hrbl_cli_read_file_contents(memory, input_path, &json_data, &json_size)) {
            bc_allocators_context_destroy(memory);
            bc_hrbl_cli_emit_stderr("bc-hrbl convert: cannot read '", input_path, "'\n", NULL, NULL);
            return 1;
        }
        void* hrbl_buffer = NULL;
        size_t hrbl_size = 0u;
        bc_hrbl_convert_error_t error;
        if (!bc_hrbl_convert_json_buffer_to_hrbl(memory, json_data, json_size, &hrbl_buffer, &hrbl_size, &error)) {
            bc_allocators_pool_free(memory, json_data);
            bc_allocators_context_destroy(memory);
            char line_buffer[24];
            char column_buffer[24];
            size_t line_length = 0;
            size_t column_length = 0;
            (void)bc_core_format_unsigned_integer_64_decimal(line_buffer, sizeof(line_buffer), error.line, &line_length);
            (void)bc_core_format_unsigned_integer_64_decimal(column_buffer, sizeof(column_buffer), error.column, &column_length);
            line_buffer[line_length] = '\0';
            column_buffer[column_length] = '\0';
            const char* message = error.message != NULL ? error.message : "invalid JSON";
            char buffer[1024];
            bc_core_writer_t writer;
            if (bc_core_writer_init_standard_error(&writer, buffer, sizeof(buffer))) {
                (void)bc_core_writer_write_cstring(&writer, "bc-hrbl convert: ");
                (void)bc_core_writer_write_cstring(&writer, message);
                (void)bc_core_writer_write_cstring(&writer, " at line ");
                (void)bc_core_writer_write_cstring(&writer, line_buffer);
                (void)bc_core_writer_write_cstring(&writer, " column ");
                (void)bc_core_writer_write_cstring(&writer, column_buffer);
                (void)bc_core_writer_write_char(&writer, '\n');
                (void)bc_core_writer_flush(&writer);
                (void)bc_core_writer_destroy(&writer);
            }
            return 1;
        }
        bc_allocators_pool_free(memory, json_data);
        if (output_path == NULL) {
            if (!bc_hrbl_cli_write_buffer_to_fd(STDOUT_FILENO, hrbl_buffer, hrbl_size)) {
                bc_hrbl_free_buffer(memory, hrbl_buffer);
                bc_allocators_context_destroy(memory);
                bc_hrbl_cli_emit_stderr("bc-hrbl convert: write failed\n", NULL, NULL, NULL, NULL);
                return 1;
            }
        } else if (!bc_hrbl_cli_write_file_contents(output_path, hrbl_buffer, hrbl_size)) {
            bc_hrbl_free_buffer(memory, hrbl_buffer);
            bc_allocators_context_destroy(memory);
            bc_hrbl_cli_emit_stderr("bc-hrbl convert: cannot write '", output_path, "'\n", NULL, NULL);
            return 1;
        }
        bc_hrbl_free_buffer(memory, hrbl_buffer);
        bc_allocators_context_destroy(memory);
        return 0;
    }

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(input_path);
    if (status != BC_HRBL_VERIFY_OK) {
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: ", input_path, ": ", bc_hrbl_verify_status_name(status), "\n");
        return 1;
    }
    bc_allocators_context_config_t config;
    (void)bc_core_zero(&config, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: allocator init failed\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, input_path, &reader)) {
        bc_allocators_context_destroy(memory);
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: cannot open '", input_path, "'\n", NULL, NULL);
        return 1;
    }
    int output_fd = STDOUT_FILENO;
    bool owns_output_fd = false;
    if (output_path != NULL) {
        output_fd = open(output_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (output_fd < 0) {
            bc_hrbl_reader_destroy(reader);
            bc_allocators_context_destroy(memory);
            bc_hrbl_cli_emit_stderr("bc-hrbl convert: cannot open output '", output_path, "'\n", NULL, NULL);
            return 1;
        }
        owns_output_fd = true;
    }
    bool ok = false;
    if (direction == BC_HRBL_CLI_CONVERT_TO_JSON) {
        ok = bc_hrbl_cli_export_via_fd(output_fd, reader, bc_hrbl_export_json);
    } else if (direction == BC_HRBL_CLI_CONVERT_TO_YAML) {
        ok = bc_hrbl_cli_export_via_fd(output_fd, reader, bc_hrbl_export_yaml);
    } else if (direction == BC_HRBL_CLI_CONVERT_TO_INI) {
        ok = bc_hrbl_cli_export_via_fd(output_fd, reader, bc_hrbl_export_ini);
    }
    if (owns_output_fd) {
        if (close(output_fd) != 0) {
            ok = false;
        }
    }
    bc_hrbl_reader_destroy(reader);
    bc_allocators_context_destroy(memory);
    if (!ok) {
        bc_hrbl_cli_emit_stderr("bc-hrbl convert: export failed\n", NULL, NULL, NULL, NULL);
        return 1;
    }
    return 0;
}

int main(int argument_count, char** argument_values)
{
    if (argument_count < 2) {
        bc_hrbl_cli_print_usage_to_fd(STDERR_FILENO);
        return 2;
    }
    const char* command = argument_values[1];
    if (bc_hrbl_cli_cstring_equal(command, "-h") || bc_hrbl_cli_cstring_equal(command, "--help")) {
        return bc_hrbl_cli_print_usage_to_fd(STDOUT_FILENO);
    }
    if (bc_hrbl_cli_cstring_equal(command, "-v") || bc_hrbl_cli_cstring_equal(command, "--version")) {
        bc_hrbl_cli_emit_stdout("bc-hrbl " BC_HRBL_CLI_VERSION_STRING "\n", NULL, NULL);
        return 0;
    }
    if (bc_hrbl_cli_cstring_equal(command, "verify")) {
        return bc_hrbl_cli_command_verify(argument_count, argument_values);
    }
    if (bc_hrbl_cli_cstring_equal(command, "query")) {
        return bc_hrbl_cli_command_query(argument_count, argument_values);
    }
    if (bc_hrbl_cli_cstring_equal(command, "inspect")) {
        return bc_hrbl_cli_command_inspect(argument_count, argument_values);
    }
    if (bc_hrbl_cli_cstring_equal(command, "convert")) {
        return bc_hrbl_cli_command_convert(argument_count, argument_values);
    }
    bc_hrbl_cli_emit_stderr("bc-hrbl: unknown command '", command, "'\n", NULL, NULL);
    bc_hrbl_cli_print_usage_to_fd(STDERR_FILENO);
    return 2;
}
