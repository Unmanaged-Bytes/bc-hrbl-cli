// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"
#include "bc_allocators.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef BC_HRBL_CLI_VERSION_STRING
#define BC_HRBL_CLI_VERSION_STRING "0.0.0-unversioned"
#endif

static int bc_hrbl_cli_print_usage(FILE* stream)
{
    fputs(
        "bc-hrbl " BC_HRBL_CLI_VERSION_STRING "\n"
        "Usage: bc-hrbl <command> [options] [arguments]\n"
        "\n"
        "Commands:\n"
        "  verify <file>                        integrity check a .hrbl file\n"
        "  query <file> <path>                  lookup a dotted path\n"
        "  inspect <file> [-o OUT]              dump as pretty JSON\n"
        "  convert --from=json IN [-o OUT]      bootstrap .hrbl from JSON\n"
        "  convert --to=json IN [-o OUT]        export .hrbl to JSON\n"
        "\n"
        "  -h, --help                           show this help and exit\n"
        "  -v, --version                        print version and exit\n",
        stream);
    return 0;
}

static bool bc_hrbl_cli_read_file_contents(const char* path, char** out_data, size_t* out_size)
{
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return false;
    }
    long size_signed = ftell(stream);
    if (size_signed < 0) {
        fclose(stream);
        return false;
    }
    size_t size = (size_t)size_signed;
    if (fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return false;
    }
    char* data = (char*)malloc(size == 0u ? 1u : size);
    if (data == NULL) {
        fclose(stream);
        return false;
    }
    if (size != 0u && fread(data, 1u, size, stream) != size) {
        free(data);
        fclose(stream);
        return false;
    }
    fclose(stream);
    *out_data = data;
    *out_size = size;
    return true;
}

static bool bc_hrbl_cli_write_file_contents(const char* path, const void* data, size_t size)
{
    FILE* stream = fopen(path, "wb");
    if (stream == NULL) {
        return false;
    }
    if (size != 0u && fwrite(data, 1u, size, stream) != size) {
        fclose(stream);
        return false;
    }
    fclose(stream);
    return true;
}

static int bc_hrbl_cli_command_verify(int argument_count, char** argument_values)
{
    if (argument_count != 3) {
        fputs("bc-hrbl verify: expected exactly one file argument\n", stderr);
        return 2;
    }
    const char* path = argument_values[2];
    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status == BC_HRBL_VERIFY_OK) {
        fprintf(stdout, "ok %s\n", path);
        return 0;
    }
    fprintf(stderr, "bc-hrbl verify: %s: %s\n", path, bc_hrbl_verify_status_name(status));
    return 1;
}

static bool bc_hrbl_cli_print_value(FILE* stream, const bc_hrbl_value_ref_t* value)
{
    bc_hrbl_kind_t kind;
    if (!bc_hrbl_reader_value_kind(value, &kind)) {
        return false;
    }
    switch (kind) {
    case BC_HRBL_KIND_NULL:
        fputs("null\n", stream);
        return true;
    case BC_HRBL_KIND_FALSE:
        fputs("false\n", stream);
        return true;
    case BC_HRBL_KIND_TRUE:
        fputs("true\n", stream);
        return true;
    case BC_HRBL_KIND_INT64: {
        int64_t v = 0;
        if (!bc_hrbl_reader_get_int64(value, &v)) {
            return false;
        }
        fprintf(stream, "%" PRId64 "\n", v);
        return true;
    }
    case BC_HRBL_KIND_UINT64: {
        uint64_t v = 0u;
        if (!bc_hrbl_reader_get_uint64(value, &v)) {
            return false;
        }
        fprintf(stream, "%" PRIu64 "\n", v);
        return true;
    }
    case BC_HRBL_KIND_FLOAT64: {
        double v = 0.0;
        if (!bc_hrbl_reader_get_float64(value, &v)) {
            return false;
        }
        fprintf(stream, "%.17g\n", v);
        return true;
    }
    case BC_HRBL_KIND_STRING: {
        const char* data = NULL;
        size_t length = 0u;
        if (!bc_hrbl_reader_get_string(value, &data, &length)) {
            return false;
        }
        if (length != 0u && fwrite(data, 1u, length, stream) != length) {
            return false;
        }
        fputc('\n', stream);
        return true;
    }
    case BC_HRBL_KIND_BLOCK:
    case BC_HRBL_KIND_ARRAY:
        return bc_hrbl_export_json(value->reader, stream);
    }
    return false;
}

static int bc_hrbl_cli_command_query(int argument_count, char** argument_values)
{
    if (argument_count != 4) {
        fputs("bc-hrbl query: expected <file> <path> arguments\n", stderr);
        return 2;
    }
    const char* path = argument_values[2];
    const char* dotted = argument_values[3];
    size_t dotted_length = strlen(dotted);

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status != BC_HRBL_VERIFY_OK) {
        fprintf(stderr, "bc-hrbl query: %s: %s\n", path, bc_hrbl_verify_status_name(status));
        return 1;
    }

    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        fputs("bc-hrbl query: allocator init failed\n", stderr);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, path, &reader)) {
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl query: cannot open '%s'\n", path);
        return 1;
    }
    bc_hrbl_value_ref_t value;
    if (!bc_hrbl_reader_find(reader, dotted, dotted_length, &value)) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl query: path not found: %s\n", dotted);
        return 1;
    }
    if (!bc_hrbl_cli_print_value(stdout, &value)) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        fputs("bc-hrbl query: cannot render value\n", stderr);
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
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argument_count) {
                fputs("bc-hrbl inspect: -o requires a path\n", stderr);
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
        fprintf(stderr, "bc-hrbl inspect: unexpected argument '%s'\n", arg);
        return 2;
    }
    if (path == NULL) {
        fputs("bc-hrbl inspect: missing input file\n", stderr);
        return 2;
    }

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(path);
    if (status != BC_HRBL_VERIFY_OK) {
        fprintf(stderr, "bc-hrbl inspect: %s: %s\n", path, bc_hrbl_verify_status_name(status));
        return 1;
    }

    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        fputs("bc-hrbl inspect: allocator init failed\n", stderr);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, path, &reader)) {
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl inspect: cannot open '%s'\n", path);
        return 1;
    }
    FILE* stream = output_path != NULL ? fopen(output_path, "wb") : stdout;
    if (stream == NULL) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl inspect: cannot open output '%s'\n", output_path);
        return 1;
    }
    bool ok = bc_hrbl_export_json(reader, stream);
    if (stream != stdout) {
        fclose(stream);
    }
    bc_hrbl_reader_destroy(reader);
    bc_allocators_context_destroy(memory);
    if (!ok) {
        fputs("bc-hrbl inspect: export failed\n", stderr);
        return 1;
    }
    return 0;
}

typedef enum bc_hrbl_cli_convert_direction {
    BC_HRBL_CLI_CONVERT_NONE = 0,
    BC_HRBL_CLI_CONVERT_FROM_JSON = 1,
    BC_HRBL_CLI_CONVERT_TO_JSON = 2
} bc_hrbl_cli_convert_direction_t;

static int bc_hrbl_cli_command_convert(int argument_count, char** argument_values)
{
    bc_hrbl_cli_convert_direction_t direction = BC_HRBL_CLI_CONVERT_NONE;
    const char* input_path = NULL;
    const char* output_path = NULL;
    for (int i = 2; i < argument_count; i += 1) {
        const char* arg = argument_values[i];
        if (strcmp(arg, "--from=json") == 0) {
            direction = BC_HRBL_CLI_CONVERT_FROM_JSON;
            continue;
        }
        if (strcmp(arg, "--to=json") == 0) {
            direction = BC_HRBL_CLI_CONVERT_TO_JSON;
            continue;
        }
        if (strcmp(arg, "-o") == 0) {
            if (i + 1 >= argument_count) {
                fputs("bc-hrbl convert: -o requires a path\n", stderr);
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
        fprintf(stderr, "bc-hrbl convert: unexpected argument '%s'\n", arg);
        return 2;
    }
    if (direction == BC_HRBL_CLI_CONVERT_NONE) {
        fputs("bc-hrbl convert: specify --from=json or --to=json\n", stderr);
        return 2;
    }
    if (input_path == NULL) {
        fputs("bc-hrbl convert: missing input file\n", stderr);
        return 2;
    }

    if (direction == BC_HRBL_CLI_CONVERT_FROM_JSON) {
        char* json_data = NULL;
        size_t json_size = 0u;
        if (!bc_hrbl_cli_read_file_contents(input_path, &json_data, &json_size)) {
            fprintf(stderr, "bc-hrbl convert: cannot read '%s'\n", input_path);
            return 1;
        }
        bc_allocators_context_config_t config;
        memset(&config, 0, sizeof(config));
        bc_allocators_context_t* memory = NULL;
        if (!bc_allocators_context_create(&config, &memory)) {
            free(json_data);
            fputs("bc-hrbl convert: allocator init failed\n", stderr);
            return 1;
        }
        void* hrbl_buffer = NULL;
        size_t hrbl_size = 0u;
        bc_hrbl_convert_error_t error;
        if (!bc_hrbl_convert_json_buffer_to_hrbl(memory, json_data, json_size, &hrbl_buffer, &hrbl_size, &error)) {
            free(json_data);
            bc_allocators_context_destroy(memory);
            fprintf(stderr, "bc-hrbl convert: %s at line %u column %u\n",
                    error.message != NULL ? error.message : "invalid JSON", error.line, error.column);
            return 1;
        }
        free(json_data);
        if (output_path == NULL) {
            if (fwrite(hrbl_buffer, 1u, hrbl_size, stdout) != hrbl_size) {
                free(hrbl_buffer);
                bc_allocators_context_destroy(memory);
                fputs("bc-hrbl convert: write failed\n", stderr);
                return 1;
            }
        } else if (!bc_hrbl_cli_write_file_contents(output_path, hrbl_buffer, hrbl_size)) {
            free(hrbl_buffer);
            bc_allocators_context_destroy(memory);
            fprintf(stderr, "bc-hrbl convert: cannot write '%s'\n", output_path);
            return 1;
        }
        free(hrbl_buffer);
        bc_allocators_context_destroy(memory);
        return 0;
    }

    bc_hrbl_verify_status_t status = bc_hrbl_verify_file(input_path);
    if (status != BC_HRBL_VERIFY_OK) {
        fprintf(stderr, "bc-hrbl convert: %s: %s\n", input_path, bc_hrbl_verify_status_name(status));
        return 1;
    }
    bc_allocators_context_config_t config;
    memset(&config, 0, sizeof(config));
    bc_allocators_context_t* memory = NULL;
    if (!bc_allocators_context_create(&config, &memory)) {
        fputs("bc-hrbl convert: allocator init failed\n", stderr);
        return 1;
    }
    bc_hrbl_reader_t* reader = NULL;
    if (!bc_hrbl_reader_open(memory, input_path, &reader)) {
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl convert: cannot open '%s'\n", input_path);
        return 1;
    }
    FILE* stream = output_path != NULL ? fopen(output_path, "wb") : stdout;
    if (stream == NULL) {
        bc_hrbl_reader_destroy(reader);
        bc_allocators_context_destroy(memory);
        fprintf(stderr, "bc-hrbl convert: cannot open output '%s'\n", output_path);
        return 1;
    }
    bool ok = bc_hrbl_export_json(reader, stream);
    if (stream != stdout) {
        fclose(stream);
    }
    bc_hrbl_reader_destroy(reader);
    bc_allocators_context_destroy(memory);
    if (!ok) {
        fputs("bc-hrbl convert: export failed\n", stderr);
        return 1;
    }
    return 0;
}

int main(int argument_count, char** argument_values)
{
    if (argument_count < 2) {
        bc_hrbl_cli_print_usage(stderr);
        return 2;
    }
    const char* command = argument_values[1];
    if (strcmp(command, "-h") == 0 || strcmp(command, "--help") == 0) {
        return bc_hrbl_cli_print_usage(stdout);
    }
    if (strcmp(command, "-v") == 0 || strcmp(command, "--version") == 0) {
        fputs("bc-hrbl " BC_HRBL_CLI_VERSION_STRING "\n", stdout);
        return 0;
    }
    if (strcmp(command, "verify") == 0) {
        return bc_hrbl_cli_command_verify(argument_count, argument_values);
    }
    if (strcmp(command, "query") == 0) {
        return bc_hrbl_cli_command_query(argument_count, argument_values);
    }
    if (strcmp(command, "inspect") == 0) {
        return bc_hrbl_cli_command_inspect(argument_count, argument_values);
    }
    if (strcmp(command, "convert") == 0) {
        return bc_hrbl_cli_command_convert(argument_count, argument_values);
    }
    fprintf(stderr, "bc-hrbl: unknown command '%s'\n", command);
    bc_hrbl_cli_print_usage(stderr);
    return 2;
}
