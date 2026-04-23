cmocka_dep = dependency('cmocka')

test_bc_hrbl_cli_smoke = executable('test_bc_hrbl_cli_smoke',
    'test_bc_hrbl_cli_smoke.c',
    dependencies : [cmocka_dep] + bc_hrbl_cli_deps,
)
test('bc_hrbl_cli_smoke', test_bc_hrbl_cli_smoke)

test_bc_hrbl_end_to_end = executable('test_bc_hrbl_end_to_end',
    'e2e/test_bc_hrbl_end_to_end.c',
    dependencies : [cmocka_dep],
    c_args : [
        '-DBC_HRBL_TEST_BINARY_PATH="' + bc_hrbl_bin.full_path() + '"',
    ],
)
test('bc_hrbl_end_to_end', test_bc_hrbl_end_to_end, depends : [bc_hrbl_bin], timeout : 30)
