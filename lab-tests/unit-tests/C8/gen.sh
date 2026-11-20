#!/bin/bash
# Generates gen/*.inc from the REAL sources (no copy-paste): tables from nr_mac_common.c, nr_ul_mcs_select_default() from
# develop (unpatched) and from branch fix/ul-mcs-clamp-table (patched). Read-only on the OAI clone.
set -eu
R=${OAI_REPO:-/workspace/oai-camara-contrib/openairinterface5g}; G=$(dirname "$0")/gen; mkdir -p $G
F=openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch_default_policies.c
extract() { # $1 = git rev, $2 = suffix
  git -C $R show $1:$F | awk '/^void nr_ul_mcs_select_default\(/{p=1} p{print} p&&/^}/{exit}' \
    | sed "s/nr_ul_mcs_select_default/nr_ul_mcs_select_default_$2/" > $G/mcs_select_$2.inc
}
extract develop unpatched
extract fix/ul-mcs-clamp-table patched
git -C $R show develop:openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.c | awk '/^static const uint16_t Table_5131[123]\[32\]\[2\]/{p=1} p{print} p&&/}};/{p=0}' > $G/tables.inc
echo "develop=$(git -C $R rev-parse --short develop) patched=$(git -C $R rev-parse --short fix/ul-mcs-clamp-table)" > $G/REVS.txt
wc -l $G/*.inc; cat $G/REVS.txt
