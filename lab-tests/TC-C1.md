# TC-C1 — OAI RRC: `get_drb()` assertion saat QoS flow terakhir pada DRB dilepas (PR #500)
Issue (review draft di fork): https://github.com/dayat81/openairinterface5g/issues/1 (target upstream: komentar di PR #500; belum diposting)
Status: DRAFT, belum dieksekusi. BUTUH BUILD gNB baru (develop & #500-head). Jangan sentuh container `n5-*` yang berjalan.

## Fakta
- Kode: `rrc_gNB_NGAP.c` ~967 (develop 11c9451), assert di `rrc_gNB_radio_bearers.c:322` (`Assertion (id > 0 && id <= (32)) failed! In get_drb()`).
- Bukti v3.0.0: `/workspace/oai-n5-test/res/c1-qose/gnb.win.log` (baris ~513-516: `removed DRB ID 0 for PDU session 10 (no remaining QoS flows mapped)` lalu assert; backtrace `get_drb` di `res/c3-qos7-delete/gnb.win.log`).
- Trigger lab: N5 (Npcf_PolicyAuthorization create/delete) via `n5proto.py`; delete app session → SMF PDU Session Resource Modify melepas QFI 2.
- Skrip: `/workspace/oai-n5-test/{run-ran.sh,cycle.sh,accept.sh}`. `run-ran.sh` memakai env `GNB_IMG` (default `oaisoftwarealliance/oai-gnb:v3.0.0`). Conf gNB: `/workspace/oai-multiue/conf/gnb-c-n78-106-2x2.conf`. `cycle.sh <tag> <profile> [mode] [protos]` (REL=1 → jalur release PDU), `accept.sh <tag> [ncycles] [profile=QOS_7]` (env DLR, ULR, HOLD, GAP, PAR) menulis `res/<tag>/checks.txt`.
- Profil→5QI di PCF: QOS_E→80, QOS_S→2, QOS_M→3, QOS_L→4, QOS_7→7.

## Prasyarat build (matriks)
| Label | Sumber | Status |
|---|---|---|
| B0 | OAI v3.0.0 (image `oaisoftwarealliance/oai-gnb:v3.0.0` + patch lab) | ada, reproduksi sudah |
| B1 | develop `11c9451` (clone: `/workspace/oai-camara-contrib/openairinterface5g`) | BELUM dibangun (~15-30 mnt, builder `lintas5g/oai-builder:tmp`) |
| B2 | PR #500 head (SHA TODO: `gh pr view 500`/`c500.json` di folder ini) di atas develop | BELUM dibangun |
- BLOCKER: skrip hardcode nama container `n5-gnb`, `n5-lab`, `n5-ue1/2`. Menjalankan B1/B2 berarti menghentikan `n5-gnb` (mengubah lab berjalan) ATAU menyalin skrip dengan nama baru (mis. `c1-gnb`) dan jaringan docker terpisah. TODO pilih salah satu dan minta jendela lab ke user.
- Jangan commit/push apa pun; tag image mis. `lintas5g/oai-gnb:develop-11c9451`, `:pr500`.
- UE tetap nrUE v3.0.0 (TODO cek kompatibilitas nrUE v3.0.0 dengan gNB develop — protokol rfsim/F1 mungkin berubah).

## Kasus uji
### TC-C1-01 — Reproduksi regresi pada B0 (kontrol)
- Langkah: `REL=0 ./cycle.sh c1-b0 QOS_7` (delete via N5; profil QOS_7 seperti c3-qos7-delete).
- Expected: gNB log memuat `removed DRB ID 0 for PDU session`, lalu `Assertion (id > 0 && id <= (32)) failed!` + `get_drb()` `rrc_gNB_radio_bearers.c:322`; gNB crash.
- Pass/fail: "bug ada di B0" bila assert muncul. Evidence: `res/c1-b0/{gnb.win.log,n5proto.out,qfi.txt,n3.pcap,smf.win.log}`.

### TC-C1-02 — Single delete pada develop 11c9451 (B1)
- Langkah: `GNB_IMG=lintas5g/oai-gnb:develop-11c9451 ./run-ran.sh` (nama container: TODO), tunggu 2 UE attach, `./cycle.sh c1-b1 QOS_7`.
- Expected (hipotesis issue): assert `get_drb()` muncul → BUG MASIH ADA di develop. Bila tidak muncul: REFUTED, jangan posting komentar (hapus klaim "develop masih punya pola yang sama" atau ubah).
- Pass/fail: hasil mana pun dicatat; ini gerbang untuk posting upstream.
- Evidence: `gnb.win.log` (grep `Assertion|get_drb|removed DRB`), core dump/backtrace jika ada, `checks.txt`.

### TC-C1-03 — Single delete pada PR #500 head (B2)
- Langkah: sama `cycle.sh c1-b2 QOS_7`.
- Expected: tidak ada assert; `gnb_assert_or_crash=0`; log `removed DRB ID <n>` dengan n valid (bukan 0).
- Pass/fail: PASS bila tidak ada assert/crash dan DRB dilepas sekali.

### TC-C1-04 — Sesi kedua pada koneksi RRC yang sama (bagian "DU release" PR)
- Langkah (B0/B1/B2): create → delete → create lagi (QOS_7 atau QOS_L) untuk UE1 tanpa re-attach (`accept.sh c1-seq 2` memakai 2 siklus); kirim probe UDP QoD port 18080.
- Expected B2: sesi 2 AVAILABLE, trafik QFI 2 lewat (`qfi-cycles.json`), 0 loss di probe; `drb_already_exists=0`, `ul_failure=0`, `gnb_tbs_null` tidak naik.
- Expected B0/B1 (bila gNB bertahan, mis. build tanpa assert): `RLC E DRB 2 already exists`, `cannot add LCID 5: already present`, `compute TBS with a NULL input`, `UL Failure on PUSCH`, loss 100% (klaim di draft).
- Evidence: `checks.txt` (field `drb_already_exists`, `ul_failure`, `gnb_tbs_null`), `delivery.jsonl`.

### TC-C1-05 — Soak 50 siklus add/remove (B2, lalu B1)
- Langkah: `DLR=20M ULR=8M ./accept.sh c1-soak50-b2 50 QOS_4?` — profil: baseline soak sebelumnya `SOAK50-q4` (profil 5QI 4 = QOS_L; TODO konfirmasi argumen persis dari `res/SOAK50-q4`).
- Expected B2: 50/50 siklus; `checks.txt`: `gnb_running=true gnb_restarted=no`, `gnb_assert_or_crash=0`, `gnb_pdu_release=0`, `amf_release=0`, `error_indication_*=0`, `ue*_ip_same=yes`, `keepalive lost=0`, `delivery_verdict PASS` (loss ≤1%). Referensi patched v3.0.0 SOAK50-q4: tbs_null=5, loss max DL 0.7/UL 0.4.
- Pass/fail: semua kriteria di atas; `gnb_tbs_null` dilaporkan terpisah (terkait C8, bukan fail C1).

### TC-C1-06 — Jalur release PDU session (negatif/regresi, bukan N5 delete)
- Langkah: `REL=1 ./cycle.sh c1-rel-b2 QOS_7` (UE1 distop → PDU release; lalu DELETE N5 diharapkan 404).
- Expected: tidak ada assert; N5 DELETE sesudahnya 404; UE2 tidak terpengaruh (keepalive 0 loss).

### TC-C1-07 — Paralel: dua UE melepas flow bersamaan
- Langkah: `PAR=1 ./accept.sh c1-par-b2 10 QOS_7` (UE2 sesi QOS_L paralel).
- Expected B2: tanpa assert; isolasi: UE2 tetap QFI 1 pada port lain.

### TC-C1-08 — Negatif: flow non-terakhir dilepas (DRB masih punya flow)
- Tujuan: pastikan jalur "no remaining QoS flows mapped" TIDAK terpicu bila DRB masih punya QFI lain.
- Langkah: dua QoS flow pada DRB yang sama. TODO: tidak jelas apakah SMF/PCF lab memetakan 2 QFI ke 1 DRB (Open5GS biasanya 1 DRB per QFI non-default). Tandai "belum bisa dijalankan" sampai ditentukan; alternatif: reproducer rfsim CU+DU+nrUE yang mengirim PDU Session Resource Modify manual (draft komentar menawarkannya; belum ditulis).

### TC-C1-09 — Reproducer mandiri tanpa core (opsional, hanya jika jadi ditulis)
- Prasyarat: reproducer rfsim CU+DU over F1 + nrUE, mengeluarkan PDU Session Resource Modify yang melepas flow terakhir DRB N kali. TODO: belum ada kode; ini syarat paragraf CI di draft komentar.

## Kriteria gerbang posting upstream
1. TC-C1-02 hasilnya diketahui (bug ada/tidak di develop).
2. TC-C1-03 + TC-C1-05 PASS pada B2.
Jika TC-C1-02 menunjukkan bug TIDAK ada di develop (sudah diperbaiki lewat #500 merge), komentar diganti laporan "dikonfirmasi tidak bisa direproduksi" atau dibatalkan.
