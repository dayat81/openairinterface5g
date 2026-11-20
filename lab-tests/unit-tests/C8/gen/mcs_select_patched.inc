void nr_ul_mcs_select_default_patched(const nr_cell_sched_t *cell, nr_ul_candidate_t *candidates, int n_candidates)
{
  const NR_bler_options_t *bo = &cell->ul_bler;
  FOR_EACH_CANDIDATE(cand, candidates, n_candidates)
  {
    int mcs;
    if (cand->is_retx) {
      mcs = cand->current_mcs;
    } else if (bo->harq_round_max == 1) {
      mcs = get_mcs_from_SINRx10(cand->mcs_table, cand->snrx10, cand->sched_pusch.nrOfLayers);
      mcs = max(bo->min_mcs, min(bo->max_mcs, min(cand->max_mcs, mcs)));
    } else if (!cand->bler_updated) {
      mcs = cand->current_mcs;
    } else {
      mcs = nr_adapt_mcs_from_bler(cand->current_mcs,
                                   bo->min_mcs,
                                   cand->max_mcs,
                                   cand->bler,
                                   bo->lower,
                                   bo->upper,
                                   cand->last_num_sched);
    }
    /* The UL MCS table can change with the search space (e.g. DCI 0_0 vs 0_1 around RA with C-RNTI),
     * so a stored MCS may be a reserved index of the table in use: never signal it */
    const int max_mcs_table = (cand->mcs_table == 0 || cand->mcs_table == 2) ? 28 : 27;
    mcs = max(0, min(mcs, max_mcs_table));
    cand->sched_pusch.mcs = mcs;
    if (!cand->is_retx)
      cand->UE->UE_sched_ctrl.ul_bler_stats.mcs = mcs;
  }
}
