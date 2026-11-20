/* C1: DRB release followed by lookup (use-after-free pattern in nr_rrc_send_e1_after_qos_update()).
 *
 * Standalone unit test with OAI's real common/utils/ds/seq_arr.c (erase shifts + zeroes the freed slot).
 *  - FIXED logic: nr_rrc_collect_released_drbs() / nr_rrc_drb_has_qos_flow() are extracted VERBATIM by gen.sh from the
 *    branch source (rrc_gNB_radio_bearers.c, cherry-picked from upstream PR `#500`); the removal step is done as in
 *    rrc_gNB_send_NGAP_PDUSESSION_MODIFY_RESP(): nr_rrc_remove_drb_by_id() for each collected id (mirrored, unchanged code).
 *  - BASELINE-UNFIXED (informational): mirror of the loop of develop 11c9451 (rrc_gNB_NGAP.c ~960-985); NOT the real file.
 * Only the surrounding types/macros are minimal shims. The real CMake target (rrc_bearers_test) is not built here.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ds/seq_arr.h"

#define MAX_DRBS_PER_UE 32
typedef struct { int status; int drb_id; int pdusession_id; } drb_t;          /* same leading fields as drb_t in nr_rrc_defs.h */
typedef struct { int qfi; int drb_id; } qos_t;
typedef qos_t nr_rrc_qos_t;                                                  /* real type has drb_id first; only drb_id is used */
typedef struct { struct { int pdusession_id; seq_arr_t qos; } param; } rrc_pdu_session_param_t;
#define DevAssert(x) assert(x)
#define LOG_E(c, ...) do { fprintf(stderr, "LOG_E " __VA_ARGS__); } while (0)
#define NR_RRC 0

static int get_drb_assert_hit;                                               /* DevAssert(id > 0 && id <= MAX_DRBS_PER_UE) */
static bool eq_drb_id(const void *v, const void *it) { return ((const drb_t *)it)->drb_id == *(const int *)v; }
static drb_t *get_drb(seq_arr_t *s, int id)
{
  if (!(id > 0 && id <= MAX_DRBS_PER_UE)) { get_drb_assert_hit++; return NULL; }
  FOR_EACH_SEQ_ARR (drb_t *, d, s) if (eq_drb_id(&id, d)) return d;
  return NULL;
}
static bool remove_drb_by_id(seq_arr_t *s, int id)
{
  drb_t *d = get_drb(s, id);
  if (!d) return false;
  seq_arr_erase_deep(s, d, NULL);
  return true;
}
static bool has_flow(const seq_arr_t *qos, int drb_id)
{
  FOR_EACH_SEQ_ARR (const qos_t *, q, qos) if (q->drb_id == drb_id) return true;
  return false;
}

typedef struct { int n; int id[MAX_DRBS_PER_UE]; } rel_t;                   /* e1_req drbs_to_remove */

/* (a) develop 11c9451. Same iteration as FOR_EACH_SEQ_ARR (it != end; it = next(it)), plus a guard: after an in-loop
 * erase the end moves back, so when the erased DRB was the last one 'next' lands PAST end and the loop would walk off the
 * array (in the real gNB it reads zeroed capacity: drb_id 0 -> get_drb(0) -> assertion). The guard records that overrun. */
static int iter_overrun;
static rel_t release_unfixed(seq_arr_t *drbs, seq_arr_t *qos, int pdu)
{
  rel_t r = {0};
  iter_overrun = 0;
  size_t guard = 0, max_it = seq_arr_size(drbs) + 1;
  for (drb_t *drb = seq_arr_front(drbs); drb != (drb_t *)seq_arr_end(drbs); drb = seq_arr_next(drbs, drb)) {
    if (++guard > max_it) { iter_overrun = 1; break; }
    if (drb->pdusession_id != pdu) continue;
    if (!has_flow(qos, drb->drb_id)) {
      remove_drb_by_id(drbs, drb->drb_id);
      r.id[r.n++] = drb->drb_id;                                            /* drb points to erased/zeroed/shifted memory */
      continue;
    }
  }
  return r;
}
#include "gen/collect.inc"      /* nr_rrc_drb_has_qos_flow(), nr_rrc_collect_released_drbs() from the branch source */
static rel_t release_fixed(seq_arr_t *drbs, seq_arr_t *qos, int pdu)
{
  rrc_pdu_session_param_t s = {.param = {.pdusession_id = pdu}};
  memcpy(&s.param.qos, qos, sizeof *qos);                 /* shallow view of the flow list (same data pointer) */
  rel_t r = {0};
  r.n = nr_rrc_collect_released_drbs(drbs, &s, r.id);
  for (int i = 0; i < r.n; i++) remove_drb_by_id(drbs, r.id[i]);   /* MODIFY_RESP step */
  return r;
}

static void add(seq_arr_t *a, int id, int pdu) { drb_t d = {.drb_id = id, .pdusession_id = pdu}; seq_arr_push_back(a, &d, sizeof d); }
static void addq(seq_arr_t *a, int qfi, int id) { qos_t q = {qfi, id}; seq_arr_push_back(a, &q, sizeof q); }

static rel_t release_unfixed(seq_arr_t *, seq_arr_t *, int);
typedef rel_t (*rel_fn)(seq_arr_t *, seq_arr_t *, int);
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("    FAIL: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* scenario helpers: drbs spec as list; qos flows reference the drb ids that are still used */
static void run(const char *name, rel_fn f, const int *drb_ids, int nd, const int *drb_pdu, const int *used, int nu,
                const int *expect_rel, int ne, int expect_left)
{
  seq_arr_t drbs, qos; seq_arr_init(&drbs, sizeof(drb_t)); seq_arr_init(&qos, sizeof(qos_t));
  for (int i = 0; i < nd; i++) add(&drbs, drb_ids[i], drb_pdu[i]);
  for (int i = 0; i < nu; i++) addq(&qos, 1 + i, used[i]);
  get_drb_assert_hit = 0;
  rel_t r = f(&drbs, &qos, 10);
  /* lookups after the release, as done later by E1/F1 handling with the released ids */
  for (int i = 0; i < r.n; i++) (void)get_drb(&drbs, r.id[i]);
  printf("  %-44s released=[", name);
  for (int i = 0; i < r.n; i++) printf("%s%d", i ? "," : "", r.id[i]);
  printf("] left=%zu get_drb_assert=%d\n", seq_arr_size(&drbs), get_drb_assert_hit);
  CHECK(!(f == release_unfixed && iter_overrun), "iterator ran past the end of the DRB array after an in-loop erase");
  CHECK(get_drb_assert_hit == 0, "get_drb() called with an invalid id (assertion rrc_gNB_radio_bearers.c:322 in the real code)");
  CHECK(r.n == ne, "released %d DRBs, expected %d", r.n, ne);
  for (int i = 0; i < ne && i < r.n; i++) CHECK(r.id[i] == expect_rel[i], "released id[%d]=%d expected %d", i, r.id[i], expect_rel[i]);
  CHECK((int)seq_arr_size(&drbs) == expect_left, "%zu DRBs left, expected %d", seq_arr_size(&drbs), expect_left);
  seq_arr_free(&drbs, NULL); seq_arr_free(&qos, NULL);
}

static int suite(const char *label, rel_fn f)
{
  printf("%s\n", label); fails = 0;
  { /* TC-C1-U1: last DRB of the session loses its flow (lab case: QFI 2 / DRB 2 deleted) */
    int ids[] = {1, 2}, pdu[] = {10, 10}, used[] = {1}, exp[] = {2};
    run("U1 release last DRB (id 2)", f, ids, 2, pdu, used, 1, exp, 1, 1); }
  { /* TC-C1-U2: first DRB released while a later one still used */
    int ids[] = {1, 2}, pdu[] = {10, 10}, used[] = {2}, exp[] = {1};
    run("U2 release first DRB, second still used", f, ids, 2, pdu, used, 1, exp, 1, 1); }
  { /* TC-C1-U3: two consecutive DRBs released (iterator skips the 2nd after an in-loop erase) */
    int ids[] = {1, 2, 3}, pdu[] = {10, 10, 10}, used[] = {3}, exp[] = {1, 2};
    run("U3 release two consecutive DRBs", f, ids, 3, pdu, used, 1, exp, 2, 1); }
  { /* TC-C1-U4 (negative): nothing to release */
    int ids[] = {1, 2}, pdu[] = {10, 10}, used[] = {1, 2};
    run("U4 all DRBs still used -> no-op", f, ids, 2, pdu, used, 2, NULL, 0, 2); }
  { /* TC-C1-U5 (negative): DRBs of another PDU session untouched */
    int ids[] = {1, 2, 3}, pdu[] = {10, 10, 11}, used[] = {1}, exp[] = {2};
    run("U5 other PDU session untouched", f, ids, 3, pdu, used, 1, exp, 1, 2); }
  { /* TC-C1-U6: release everything */
    int ids[] = {1}, pdu[] = {10}, exp[] = {1};
    run("U6 sole DRB released -> empty list", f, ids, 1, pdu, NULL, 0, exp, 1, 0); }
  { /* TC-C1-U7: repeated add/release cycles stay consistent (lab: 50 cycles) */
    seq_arr_t drbs, qos; seq_arr_init(&drbs, sizeof(drb_t)); seq_arr_init(&qos, sizeof(qos_t));
    int bad = 0; get_drb_assert_hit = 0;
    add(&drbs, 1, 10); addq(&qos, 1, 1);
    for (int c = 0; c < 50; c++) {
      add(&drbs, 2, 10);                                  /* new on-demand flow gets DRB 2, then the flow is deleted */
      rel_t r = f(&drbs, &qos, 10);
      for (int i = 0; i < r.n; i++) (void)get_drb(&drbs, r.id[i]);
      if (r.n != 1 || r.id[0] != 2 || seq_arr_size(&drbs) != 1) bad++;
    }
    printf("  %-44s bad_cycles=%d/50 get_drb_assert=%d\n", "U7 50 add/release cycles", bad, get_drb_assert_hit);
    CHECK(bad == 0, "%d of 50 cycles inconsistent", bad); CHECK(get_drb_assert_hit == 0, "get_drb assertion hit");
    seq_arr_free(&drbs, NULL); seq_arr_free(&qos, NULL); }
  printf("  => %s (%d failed check(s))\n", fails ? "FAIL" : "PASS", fails);
  return fails;
}

int main(void)
{
  int unfixed = suite("[baseline-unfixed (informational): develop 11c9451 mirror] expected: FAIL (bug)", release_unfixed);
  int fixed = suite("[fixed: branch source, cherry-picked PR `#500`] expected: PASS", release_fixed);
  printf("\nSUMMARY: baseline-unfixed (informational)=%s fixed=%s\n", unfixed ? "FAIL(as expected)" : "PASS(bug not reproduced by mirror)", fixed ? "FAIL" : "PASS");
  return fixed ? 1 : 0;      /* exit status depends ONLY on the fixed code */
}
