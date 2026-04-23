bc_hrbl_cli_sources = [
    'bc_hrbl_main.c',
]

bc_hrbl_cli_version_arg = '-DBC_HRBL_CLI_VERSION_STRING="@0@"'.format(meson.project_version())

bc_hrbl_bin = executable('bc-hrbl',
    bc_hrbl_cli_sources,
    include_directories : [bc_hrbl_cli_internal_inc],
    dependencies : bc_hrbl_cli_deps,
    c_args : lto_c_args + [bc_hrbl_cli_version_arg],
    link_args : lto_link_args,
    install : true,
)
