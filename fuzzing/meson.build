# SPDX-License-Identifier: MIT

fuzzer_c_args = []
fuzzer_link_args = []
if cc.get_id() == 'clang'
    fuzzer_c_args += ['-fsanitize=fuzzer', '-DBC_FUZZ_LIBFUZZER']
    fuzzer_link_args += ['-fsanitize=fuzzer']
endif

# JSON convert fuzz harness wired in Phase 4 once the convertisseur is in place.
