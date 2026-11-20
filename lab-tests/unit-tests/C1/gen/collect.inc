static bool nr_rrc_drb_has_qos_flow(const rrc_pdu_session_param_t *session, int drb_id)
{
  DevAssert(session);
  FOR_EACH_SEQ_ARR (const nr_rrc_qos_t *, q, &session->param.qos) {
    if (q->drb_id == drb_id)
      return true;
  }
  return false;
}

int nr_rrc_collect_released_drbs(const seq_arr_t *drbs, const rrc_pdu_session_param_t *session, int out[MAX_DRBS_PER_UE])
{
  DevAssert(drbs);
  DevAssert(session);
  DevAssert(out);
  int n = 0;
  FOR_EACH_SEQ_ARR (const drb_t *, drb, drbs) {
    if (drb->pdusession_id != session->param.pdusession_id)
      continue;
    if (nr_rrc_drb_has_qos_flow(session, drb->drb_id))
      continue;
    if (n == MAX_DRBS_PER_UE) {
      LOG_E(NR_RRC, "PDU Session %d: more than %d released DRBs\n", session->param.pdusession_id, MAX_DRBS_PER_UE);
      break;
    }
    out[n++] = drb->drb_id;
  }
  return n;
}

