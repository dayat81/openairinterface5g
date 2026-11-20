/* C8: UL MCS must be a non-reserved index of the ACTIVE UL MCS table, otherwise TBS = 0 ("compute TBS with a NULL input").
 *
 * The code under test is NOT re-typed: gen.sh extracts nr_ul_mcs_select_default() verbatim from
 *   - develop 11c9451                     -> *_unpatched   (expected to FAIL the stale-MCS cases)
 *   - branch fix/ul-mcs-clamp-table       -> *_patched     (expected to PASS)
 * and the MCS tables Table_51311/2/3 verbatim from nr_mac_common.c. Only the surrounding types are minimal shims with
 * the same field names; nr_adapt_mcs_from_bler()/get_mcs_from_SINRx10() are simple stubs (they are not the subject).
 * The "NULL TBS" predicate mirrors nr_compute_tbs_common.c:77 (Qm == 0 || R == 0 || nb_rb == 0 || Nl == 0).
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define FOR_EACH_CANDIDATE(VaR, ArR, N) for (__typeof__(*(ArR)) *VaR = (ArR); VaR < (ArR) + (N); VaR++)

typedef struct { int harq_round_max, min_mcs, max_mcs; float lower, upper; } NR_bler_options_t;
typedef struct { NR_bler_options_t ul_bler; } nr_cell_sched_t;
typedef struct { struct { struct { int mcs; } ul_bler_stats; } UE_sched_ctrl; } NR_UE_info_t;
typedef struct {
  NR_UE_info_t *UE;
  bool is_retx, bler_updated;
  int current_mcs, mcs_table, snrx10, max_mcs, last_num_sched;
  float bler;
  struct { int nrOfLayers, mcs; } sched_pusch;
} nr_ul_candidate_t;

static int get_mcs_from_SINRx10(int table, int snrx10, int layers) { (void)table; (void)layers; return snrx10 / 10; }
static int nr_adapt_mcs_from_bler(int cur, int min_mcs, int max_mcs, float bler, float lo, float hi, int n)
{ (void)n; int m = cur; if (bler < lo) m++; else if (bler > hi) m--; return max(min_mcs, min(max_mcs, m)); }

#include "gen/tables.inc"       /* static const uint16_t Table_51311/51312/51313[32][2] = {Qm, R x 10} */
#include "gen/mcs_select_unpatched.inc"
#include "gen/mcs_select_patched.inc"

/* table index as used by OAI: 0 = qam64 (5.1.3.1-1), 1 = qam256 (-2), 2 = qam64LowSE (-3) */
static const uint16_t (*tab(int t))[2] { return t == 0 ? Table_51311 : t == 1 ? Table_51312 : Table_51313; }
static bool tbs_is_null(int table, int mcs)           /* nb_rb=1, Nl=1 are valid; only Qm/R decide */
{ if (mcs < 0 || mcs > 31) return true; return tab(table)[mcs][0] == 0 || tab(table)[mcs][1] == 0; }
static int valid_max(int table) { return table == 1 ? 27 : 28; }   /* highest non-reserved index (= clamp rule of the patch) */

typedef void (*sel_fn)(const nr_cell_sched_t *, nr_ul_candidate_t *, int);
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("    FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static nr_ul_candidate_t mk(NR_UE_info_t *ue, int table, int cur, bool retx, bool upd)
{
  nr_ul_candidate_t c = {.UE = ue, .is_retx = retx, .bler_updated = upd, .current_mcs = cur, .mcs_table = table,
                         .snrx10 = 200, .max_mcs = valid_max(table), .last_num_sched = 5, .bler = 0.1f};
  c.sched_pusch.nrOfLayers = 1; return c;
}

static void case_stale(sel_fn f, const char *who)
{
  NR_UE_info_t ue; nr_cell_sched_t cell = {.ul_bler = {.harq_round_max = 4, .min_mcs = 0, .max_mcs = 28, .lower = 0.1f, .upper = 0.2f}};
  /* U1/U2: stale MCS (from the other table) on retx path and on !bler_updated path, for every table and every index 0..31 */
  for (int path = 0; path < 2; path++)
    for (int t = 0; t < 3; t++) {
      int bad = 0;
      for (int stale = 0; stale <= 31; stale++) {
        memset(&ue, 0, sizeof ue);
        nr_ul_candidate_t c = mk(&ue, t, stale, path == 0, path == 1 ? false : true);
        f(&cell, &c, 1);
        if (tbs_is_null(t, c.sched_pusch.mcs)) bad++;
      }
      CHECK(bad == 0, "%s path=%s table=%d: %d/32 stale inputs give a reserved/NULL-TBS MCS", who, path == 0 ? "retx" : "!bler_updated", t, bad);
    }
}
static void case_bler_path(sel_fn f, const char *who)    /* regression: normal BLER adaptation unchanged and in range */
{
  NR_UE_info_t ue; nr_cell_sched_t cell = {.ul_bler = {.harq_round_max = 4, .min_mcs = 0, .max_mcs = 28, .lower = 0.1f, .upper = 0.2f}};
  for (int t = 0; t < 3; t++)
    for (int cur = 0; cur <= valid_max(t); cur++) {
      memset(&ue, 0, sizeof ue);
      nr_ul_candidate_t c = mk(&ue, t, cur, false, true); c.bler = 0.01f;       /* below lower: step up, bounded by cand->max_mcs */
      f(&cell, &c, 1);
      CHECK(c.sched_pusch.mcs == min(cur + 1, valid_max(t)) && !tbs_is_null(t, c.sched_pusch.mcs), "%s bler path table=%d cur=%d -> %d", who, t, cur, c.sched_pusch.mcs);
      CHECK(ue.UE_sched_ctrl.ul_bler_stats.mcs == c.sched_pusch.mcs, "%s stored MCS differs from signalled MCS", who);
    }
}
static void case_valid_unchanged(sel_fn f, const char *who) /* regression: valid stale values pass through untouched */
{
  NR_UE_info_t ue; nr_cell_sched_t cell = {.ul_bler = {.harq_round_max = 4, .min_mcs = 0, .max_mcs = 28, .lower = 0.1f, .upper = 0.2f}};
  for (int t = 0; t < 3; t++)
    for (int m = 0; m <= valid_max(t); m++) {
      memset(&ue, 0, sizeof ue);
      nr_ul_candidate_t c = mk(&ue, t, m, true, true);
      f(&cell, &c, 1);
      CHECK(c.sched_pusch.mcs == m, "%s retx table=%d valid mcs %d changed to %d", who, t, m, c.sched_pusch.mcs);
    }
}
static void case_stored_mcs(sel_fn f, const char *who)      /* the MCS persisted for the next slot must not be reserved either */
{
  NR_UE_info_t ue; nr_cell_sched_t cell = {.ul_bler = {.harq_round_max = 4, .min_mcs = 0, .max_mcs = 28, .lower = 0.1f, .upper = 0.2f}};
  memset(&ue, 0, sizeof ue);
  nr_ul_candidate_t c = mk(&ue, 1, 28, false, false);        /* qam256 active, stored MCS 28 came from qam64 */
  f(&cell, &c, 1);
  CHECK(!tbs_is_null(1, ue.UE_sched_ctrl.ul_bler_stats.mcs), "%s ul_bler_stats.mcs=%d reserved in table 1", who, ue.UE_sched_ctrl.ul_bler_stats.mcs);
}
static void case_multi(sel_fn f, const char *who)           /* several candidates: one bad must not affect the others */
{
  NR_UE_info_t ue[3]; nr_cell_sched_t cell = {.ul_bler = {.harq_round_max = 4, .min_mcs = 0, .max_mcs = 28, .lower = 0.1f, .upper = 0.2f}};
  memset(ue, 0, sizeof ue);
  nr_ul_candidate_t c[3] = {mk(&ue[0], 1, 31, true, true), mk(&ue[1], 0, 10, true, true), mk(&ue[2], 2, 29, false, false)};
  f(&cell, c, 3);
  CHECK(c[1].sched_pusch.mcs == 10, "%s unaffected candidate changed", who);
  CHECK(!tbs_is_null(1, c[0].sched_pusch.mcs) && !tbs_is_null(2, c[2].sched_pusch.mcs), "%s multi: reserved MCS left (%d,%d)", who, c[0].sched_pusch.mcs, c[2].sched_pusch.mcs);
}
static void case_tables_have_reserved_rows(void)           /* sanity of the fixture: the claim of the issue about the tables */
{
  CHECK(tbs_is_null(1, 28) && tbs_is_null(1, 31) && !tbs_is_null(1, 27), "table1 reserved rows");
  CHECK(tbs_is_null(0, 29) && !tbs_is_null(0, 28), "table0 reserved rows");
  CHECK(tbs_is_null(2, 29) && !tbs_is_null(2, 28), "table2 reserved rows");
  CHECK(tab(1)[28][0] == 2 && tab(1)[28][1] == 0, "table1[28] is {Qm 2, R 0} as in the issue");
}

static int suite(const char *label, sel_fn f, const char *who, bool include_stale)
{
  fails = checks = 0; printf("%s\n", label);
  if (include_stale) { case_stale(f, who); case_stored_mcs(f, who); case_multi(f, who); }
  case_bler_path(f, who); case_valid_unchanged(f, who);
  printf("  => %s (%d/%d checks failed)\n", fails ? "FAIL" : "PASS", fails, checks);
  return fails;
}
int main(void)
{
  fails = checks = 0; case_tables_have_reserved_rows();
  if (fails) { printf("fixture sanity FAILED\n"); return 2; }
  int un_stale = suite("[develop 11c9451] stale-MCS cases: expected FAIL (bug)", nr_ul_mcs_select_default_unpatched, "unpatched", true);
  int un_reg = suite("[develop 11c9451] regression-only cases: expected PASS", nr_ul_mcs_select_default_unpatched, "unpatched", false);
  int pa = suite("[fix/ul-mcs-clamp-table 80cff92] all cases: expected PASS", nr_ul_mcs_select_default_patched, "patched", true);
  printf("\nSUMMARY: unpatched stale cases=%s, unpatched regression=%s, patched all=%s\n",
         un_stale ? "FAIL(as expected)" : "PASS(UNEXPECTED: bug not reproduced)", un_reg ? "FAIL(UNEXPECTED)" : "PASS", pa ? "FAIL(UNEXPECTED)" : "PASS");
  return (un_stale && !un_reg && !pa) ? 0 : 1;
}
