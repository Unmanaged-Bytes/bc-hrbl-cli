cmocka_dep = dependency('cmocka')

test_bc_hrbl_cli_smoke = executable('test_bc_hrbl_cli_smoke',
    'test_bc_hrbl_cli_smoke.c',
    dependencies : [cmocka_dep] + bc_hrbl_cli_deps,
)
test('bc_hrbl_cli_smoke', test_bc_hrbl_cli_smoke)
