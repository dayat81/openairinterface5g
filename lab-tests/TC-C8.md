# TC-C8 — OAI NR MAC: UL MCS stale (indeks reserved) pada tabel MCS UL aktif → TBS=0
Issue (review draft di fork): https://github.com/dayat81/openairinterface5g/issues/2 (belum diposting upstream)
Status: DRAFT, belum dieksekusi. BUTUH BUILD (develop, develop+patch). Kondisional: tidak boleh diposting sebelum TC-C8-02 mereproduksi di develop.

## Fakta
- Kode: `openair2/LAYER2/NR_MAC_gNB/gNB_scheduler_ulsch_default_policies.c`, `nr_ul_mcs_select_default()` ~230-256.
- Patch: branch `fix/ul-mcs-clamp-table` (commit 80cff92) di `/workspace/oai-camara-contrib/openairinterface5g`, file `/workspace/oai-camara-contrib/out/0001-NR-MAC-clamp-UL-MCS-to-the-active-MCS-table-in-defau.patch`. BELUM dikompilasi.
- Gejala log (gNB): `[NR_MAC] E Error in compute TBS with a NULL input, returning NULL TBS`; `UL Failure on PUSCH`; sisi UE `Invalid TBS = 0`/DTX.
- Evidensi lama (confounded, v3.0.0+slice patch): NULL TBS 743 → 5, UL failure 161 → 0 pada soak 50 siklus. `res/SOAK50-q4` masih `gnb_tbs_null=5`; `res/dbg-tbs-12M` ada (TODO belum dibaca).
- Skrip: `accept.sh <tag> 50 <profile>` dengan `DLR=20M ULR=8M` (profil SOAK50-q4 = 5QI 4/QOS_L, TODO konfirmasi), `checks.txt` memuat `gnb_tbs_null`, `ul_failure`, `rrc_setup_or_reest`.

## Matriks build
| Label | Sumber | Status |
|---|---|---|
| D0 | develop 11c9451 | belum dibangun |
| D1 | D0 + commit 80cff92 (clamp) | belum dibangun; juga uji kompilasi |
| D2 | develop + patch "no-slice" (tanpa patch slice lab) | pastikan tidak ada perubahan scheduler lain ikut (isolasi variabel) |
- Blocker sama dengan TC-C1: skrip hardcode container `n5-*`; nrUE v3.0.0 vs gNB develop kompatibilitas TODO; butuh jendela lab dari user.
- Sifat stokastik: bug muncul setelah UL failure + RA dengan C-RNTI (pergantian DCI 0_0↔0_1). Setiap konfigurasi dijalankan ≥3 kali (seed/timing berbeda); catat distribusi, bukan satu run.

## Kasus uji
### TC-C8-01 — Kompilasi patch + lint
- Langkah: build D1 (target nr-softmodem). `git -C openairinterface5g diff develop..fix/ul-mcs-clamp-table --stat` harus 4 baris.
- Expected: build sukses tanpa warning baru di file itu; `max`/`min` macro tersedia (TODO cek, draft memakai `max(0, min(...))`).
- Pass/fail: build hijau.

### TC-C8-02 — GERBANG: reproduksi di develop D0 (soak penuh)
- Langkah: `DLR=20M ULR=8M ./accept.sh c8-d0-r1 50 <QOS_L>` (×3 run: r1..r3), 2 nrUE, Open5GS.
- Expected (hipotesis): `gnb_tbs_null` > 0 dan `ul_failure` > 0 pada ≥1 run.
- Pass/fail: "reproduksi" bila `gnb_tbs_null` ≥ 1 dengan korelasi `UL Failure` → RA → NULL TBS di log (kronologi). Jika ketiganya 0: REFUTED → jangan posting; hapus issue fork atau tandai "tidak reproduksi".
- Evidence: `res/c8-d0-r*/{checks.txt,gnb.win.log,ue1.win.log,ue2.win.log,n3.pcap,delivery.jsonl}`; ekstrak log MCS: TODO (butuh log level MAC `-d`/`--log_config` untuk mencetak MCS & tabel; belum dikonfirmasi flag yang tersedia di conf).

### TC-C8-03 — Efek patch pada D1 (A/B terhadap TC-C8-02)
- Langkah: sama dengan -02, tag `c8-d1-r1..r3`.
- Expected: `gnb_tbs_null` jauh lebih kecil (target 0), `ul_failure` = 0.
- Pass/fail: PASS bila total NULL TBS D1 ≤ 10% dari D0 dan UL failure 0 pada ketiga run. Bila D0 juga 0, kasus tidak bermakna (lihat gerbang).

### TC-C8-04 — Regresi throughput: patch tidak menurunkan UL normal
- Langkah: `ULR` 8M tanpa RA/failure (run pendek, mis. `accept.sh c8-reg 5`) pada D0 vs D1.
- Expected: `delivery_verdict PASS`; UL throughput iperf (`iperf-ue1-ul.txt`) D1 ≥ 95% D0.

### TC-C8-05 — Negatif: tabel MCS 64QAM vs 256QAM
- Tujuan: clamp 28 untuk tabel 0/2, 27 untuk lainnya. Langkah: jalankan D1 dengan `mcs_table` yang berbeda di conf gNB (copy conf, jangan ubah yang asli). TODO: nama parameter di `gnb-c-n78-106-2x2.conf` (`pusch_...`/`ul_mcs_table`?) belum diverifikasi.
- Expected: tidak ada MCS > batas di DCI; tak ada NULL TBS.

### TC-C8-06 — Isolasi variabel (lawan confound)
- Langkah: D2 = develop + patch clamp saja vs D0, serta (jika ingin) v3.0.0 + clamp saja vs v3.0.0 + slice patch tanpa clamp. Bandingkan `gnb_tbs_null`.
- Expected: efek clamp terisolasi; hasil menentukan klaim "743→5" boleh dipakai atau dibuang dari issue.

### TC-C8-07 — Kondisi tepi: retx dengan MCS lama (jalur `cand->is_retx`)
- Tujuan: jalur retx (`mcs = cand->current_mcs`) tidak ter-clamp pada develop. Langkah: paksa UL BLER tinggi (TODO: mekanisme injeksi — mis. atenuasi rfsim `--rfsimulator.options chanmod`/`noise_power_dB`, belum diverifikasi) agar banyak HARQ retx.
- Expected D1: tidak ada MCS reserved yang disinyalkan; D0 mungkin menunjukkan NULL TBS.

## Blocker / TODO
- Build D0/D1 belum ada; jendela lab; kompatibilitas nrUE; flag logging MCS; mekanisme injeksi UL failure.
