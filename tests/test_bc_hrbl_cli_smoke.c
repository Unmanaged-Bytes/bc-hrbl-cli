// SPDX-License-Identifier: MIT

#include "bc_hrbl.h"

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

static void test_bc_hrbl_library_headers_available(void** state)
{
    (void)state;
    assert_int_equal((int)BC_HRBL_VERSION_MAJOR, 1);
    assert_int_equal((int)BC_HRBL_VERSION_MINOR, 0);
    assert_int_equal((int)BC_HRBL_MAGIC, 0x4C425248);
}

static void test_bc_hrbl_verify_error_names(void** state)
{
    (void)state;
    assert_string_equal(bc_hrbl_verify_status_name(BC_HRBL_VERIFY_OK), "ok");
    assert_string_equal(bc_hrbl_verify_status_name(BC_HRBL_VERIFY_ERR_BAD_MAGIC), "bad_magic");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_bc_hrbl_library_headers_available),
        cmocka_unit_test(test_bc_hrbl_verify_error_names),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
