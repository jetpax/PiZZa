#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# Inject the PiZZa Zephyr dual-map branch into a build-dir copy of
# dosbox-x's cpu/dynamic_alloc_common.h, keeping the checkout pristine
# (same gen-copy discipline as the CMake sed rewrites). The branch runs
# as the first allocation attempt in cache_dynamic_common_alloc(); on
# success every downstream "if (cache_code_start_ptr == NULL)" path is
# skipped.
import sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()

# file-scope prototype (linkage spec can't live inside a function body)
proto_anchor = '#include "logging.h"\n'
proto = proto_anchor + (
    '\n/* PiZZa: dual-map code-cache backing (src/dosbox_dyncache.c) */\n'
    'extern "C" uint8_t *pizza_dyncache_map(size_t size, uint8_t **exec_ptr);\n'
)

# allocation branch, injected as the first attempt
branch_anchor = "    dyncore_alloc = DYNCOREALLOC_NONE;\n"
branch = branch_anchor + """
#if defined(__ZEPHYR__)
    /* PiZZa: static arena mapped RW+RX (P0 dual-map W^X). */
    {
        uint8_t *rx = NULL;
        uint8_t *rw = pizza_dyncache_map((size_t)actualsz, &rx);
        if (rw != NULL) {
            cache_code_start_ptr = rw;
            cache_exec_ptr = rx;
            dyncore_alloc = DYNCOREALLOC_NONE;
            dyncore_method = DYNCOREM_DUAL_RW_X;
            dyncore_flags |= DYNCOREF_W_XOR_X;
        }
    }
#endif
"""

for name, anchor in (("proto", proto_anchor), ("branch", branch_anchor)):
    if text.count(anchor) != 1:
        sys.exit("patch_dyncache: %s anchor not unique in %s" % (name, src))

text = text.replace(proto_anchor, proto, 1)
text = text.replace(branch_anchor, branch, 1)
open(dst, "w").write(text)
