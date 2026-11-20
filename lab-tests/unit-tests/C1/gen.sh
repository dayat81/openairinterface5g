#!/bin/bash
# Extracts VERBATIM nr_rrc_drb_has_qos_flow() and nr_rrc_collect_released_drbs() from the checked-out branch source
# (openair2/RRC/NR/rrc_gNB_radio_bearers.c, cherry-picked from upstream PR `#500`) into gen/collect.inc.
set -eu
R=${OAI_REPO:-$(cd "$(dirname "$0")" && git rev-parse --show-toplevel)}; G=$(dirname "$0")/gen; mkdir -p $G
F=$R/openair2/RRC/NR/rrc_gNB_radio_bearers.c
awk '/^static bool nr_rrc_drb_has_qos_flow\(/{p=1} /^int nr_rrc_collect_released_drbs\(/{p=1} p{print} p&&/^}/{p=0; print ""}' $F > $G/collect.inc
grep -q nr_rrc_collect_released_drbs $G/collect.inc || { echo "nr_rrc_collect_released_drbs not found in $F (branch without the #500 fix?)"; exit 1; }
echo "extracted from $F @ $(git -C $R rev-parse --short HEAD 2>/dev/null)" > $G/REVS.txt; cat $G/REVS.txt; wc -l $G/collect.inc
