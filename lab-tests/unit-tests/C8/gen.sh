#!/bin/bash
# Regenerates gen/*.inc from REAL sources (no copy-paste):
#   mcs_select_patched.inc   : nr_ul_mcs_select_default() from the CHECKED-OUT branch source (contains the 4-line clamp)
#   mcs_select_unpatched.inc : same function from BASE_REV (default origin/develop, i.e. develop 11c9451 without the clamp)
#   tables.inc               : Table_51311/2/3 from nr_mac_common.c of the checked-out branch
# Read-only on the repo. Needs `git fetch --depth 1 origin develop` in a shallow clone so BASE_REV exists.
set -eu
R=${OAI_REPO:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)}; G=$(dirname "$0")/gen; mkdir -p $G
BASE_REV=${BASE_REV:-origin/develop}
F=openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch_default_policies.c
ext() { awk '/^void nr_ul_mcs_select_default\(/{p=1} p{print} p&&/^}/{exit}' | sed "s/nr_ul_mcs_select_default/nr_ul_mcs_select_default_$1/"; }
ext patched < $R/$F > $G/mcs_select_patched.inc
git -C $R show $BASE_REV:$F | ext unpatched > $G/mcs_select_unpatched.inc
awk '/^static const uint16_t Table_5131[123]\[32\]\[2\]/{p=1} p{print} p&&/}};/{p=0}' $R/openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.c > $G/tables.inc
grep -q "max_mcs_table" $G/mcs_select_patched.inc || { echo "WARNING: clamp not present in checked-out source (branch without the fix)"; }
echo "patched=$(git -C $R rev-parse --short HEAD) (working tree) base=$BASE_REV@$(git -C $R rev-parse --short $BASE_REV)" > $G/REVS.txt
wc -l $G/*.inc; cat $G/REVS.txt
