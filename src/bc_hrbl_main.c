// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

#include <stdio.h>
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
        "  verify <file>                 integrity check a .hrbl file\n"
        "  query <file> <path>           lookup a dotted path\n"
        "  inspect <file>                dump as pretty JSON\n"
        "  convert --from=json IN --to OUT | --to=json IN --to OUT\n"
        "\n"
        "  -h, --help                    show this help and exit\n"
        "  -v, --version                 print version and exit\n",
        stream);
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

    if (strcmp(command, "verify") == 0 ||
        strcmp(command, "query") == 0 ||
        strcmp(command, "inspect") == 0 ||
        strcmp(command, "convert") == 0) {
        fprintf(stderr, "bc-hrbl: command '%s' is wired in phase 5 of the v1.0.0 milestone.\n", command);
        return 2;
    }

    fprintf(stderr, "bc-hrbl: unknown command '%s'\n", command);
    bc_hrbl_cli_print_usage(stderr);
    return 2;
}
