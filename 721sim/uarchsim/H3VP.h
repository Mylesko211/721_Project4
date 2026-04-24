#pragma once
#include "VPBase.h"
#include <cstdint>
#include <vector>
#include <cassert>

// History table entry – one slot per (hashed) PC.
struct H3VP_entry {
   uint64_t tag;       // tag for collision detection (tag_bits wide in HW)
   int64_t  hist0;     // last-retired value
   int64_t  hist1;     // second-to-last-retired value
   int64_t  hist2;     // third-to-last-retired value
   int64_t  diff;      // hist0 − prev_hist0 (arithmetic stride)
   int64_t  instance;  // in-flight instance counter (for OOO)
   uint8_t  num_ret;   // values retired so far (capped at 3)

   // Per-predictor confidence counters and failure flags.
   // On failure: conf resets to 0 and failed flag is set (raise threshold to conf2).
   uint16_t conf_arith;
   uint16_t conf_2per;
   uint16_t conf_3per;
   bool failed_arith;
   bool failed_2per;
   bool failed_3per;
};

struct H3VP_VPQ_entry {
   uint64_t pc;
   int64_t  val;  // set to predicted value at allocation; overwritten with actual at execute
};

// H3VP: History-Based Highly Reliable Hybrid Value Predictor
// Combines three individual predictors over a shared history table:
//   (1) Arithmetic (stride) predictor
//   (2) Two-periodic predictor
//   (3) Three-periodic predictor
// Two-step confidence thresholds: conf1 (initial) and conf2 (after any misprediction).
class H3VP : public VPBase {
public:
   uint64_t qsize;
   bool     oracleconf;
   uint64_t index_bits;
   uint64_t tag_bits;
   uint64_t conf1;   // 1st confidence threshold (lower, used until first misprediction)
   uint64_t conf2;   // 2nd confidence threshold (higher, used after any misprediction)

   bool pred_int_alu;
   bool pred_fp_alu;
   bool pred_load;

   // Value Prediction Queue (VPQ) – circular FIFO
   uint64_t VPQ_head;
   uint64_t VPQ_tail;
   bool     VPQ_head_phase;
   bool     VPQ_tail_phase;
   std::vector<H3VP_VPQ_entry> VPQ;

   // History table (direct-mapped with optional tags)
   std::vector<H3VP_entry> table;

   uint64_t index_mask;
   uint64_t tag_mask;

   H3VP(uint64_t qsize, bool oracleconf, uint64_t index_bits, uint64_t tag_bits,
        uint64_t conf1, uint64_t conf2,
        bool pred_int_alu, bool pred_fp_alu, bool pred_load);
   ~H3VP() override;

   // VPBase interface
   bool     is_eligible(bool is_int_alu, bool is_fp_alu, bool is_load) override;
   int      get_VPQ_free_entries() override;
   void     checkpoint(uint64_t &tail, bool &phase) override;
   int64_t  install_VPQ(uint64_t pc) override;
   bool     search(uint64_t pc) override;
   int      get_confidence(uint64_t pc) override;
   void     update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value) override;
   bool     retire_VPQ(uint64_t pc, int64_t true_value) override;
   void     partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new) override;
   void     full_rollback() override;
   uint64_t get_table_storage_bytes() const override;

   // Internal helpers
   uint64_t get_index(uint64_t pc) const;
   uint64_t get_tag(uint64_t pc) const;
   bool     tag_match(uint64_t pc) const;
   int      get_inflight_instances(uint64_t pc) const;
   void     install_entry(uint64_t pc, int64_t true_value);
   uint64_t get_bits(uint64_t value) const;
};
