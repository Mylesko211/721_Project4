#include "H3VP.h"
#include <algorithm>
#include <cstring>
#include <cstdio>

H3VP::H3VP(uint64_t qsize, bool oracleconf, uint64_t index_bits, uint64_t tag_bits,
            uint64_t conf1, uint64_t conf2,
            bool pred_int_alu, bool pred_fp_alu, bool pred_load)
{
   this->qsize       = qsize;
   this->oracleconf  = oracleconf;
   this->index_bits  = index_bits;
   this->tag_bits    = tag_bits;
   this->conf1       = conf1;
   this->conf2       = conf2;
   this->pred_int_alu = pred_int_alu;
   this->pred_fp_alu  = pred_fp_alu;
   this->pred_load    = pred_load;

   VPQ_head = VPQ_tail = 0;
   VPQ_head_phase = VPQ_tail_phase = false;

   index_mask = ((1ULL << index_bits) - 1ULL) << 2;
   tag_mask   = tag_bits ? (((1ULL << tag_bits) - 1ULL) << (2 + index_bits)) : 0ULL;

   VPQ.resize(qsize);
   for (auto &e : VPQ) { e.pc = 0; e.val = 0; }

   uint64_t num_entries = 1ULL << index_bits;
   table.resize(num_entries);
   for (auto &e : table) {
      e.tag        = 0;
      e.hist0 = e.hist1 = e.hist2 = e.diff = 0;
      e.instance   = 0;
      e.num_ret    = 0;
      e.conf_arith = e.conf_2per = e.conf_3per = 0;
      e.failed_arith = e.failed_2per = e.failed_3per = false;
   }
}

H3VP::~H3VP() {}

bool H3VP::is_eligible(bool is_int_alu, bool is_fp_alu, bool is_load) {
   return (is_int_alu && pred_int_alu) ||
          (is_fp_alu  && pred_fp_alu)  ||
          (is_load    && pred_load);
}

uint64_t H3VP::get_index(uint64_t pc) const {
   return (pc & index_mask) >> 2;
}

uint64_t H3VP::get_tag(uint64_t pc) const {
   if (tag_bits == 0) return 0;
   return (pc & tag_mask) >> (2 + index_bits);
}

bool H3VP::tag_match(uint64_t pc) const {
   if (tag_bits == 0) return true;
   return table[get_index(pc)].tag == get_tag(pc);
}

bool H3VP::search(uint64_t pc) {
   return tag_match(pc);
}

int H3VP::get_VPQ_free_entries() {
   if (VPQ_head == VPQ_tail && VPQ_head_phase != VPQ_tail_phase)
      return 0;
   if (VPQ_head == VPQ_tail)
      return (int)qsize;
   if (VPQ_tail > VPQ_head && VPQ_tail_phase == VPQ_head_phase)
      return (int)(qsize - (VPQ_tail - VPQ_head));
   return (int)(VPQ_head - VPQ_tail);
}

void H3VP::checkpoint(uint64_t &tail, bool &phase) {
   tail  = VPQ_tail;
   phase = VPQ_tail_phase;
}

int H3VP::get_inflight_instances(uint64_t pc) const {
   int count = 0;
   uint64_t i = VPQ_head;
   bool i_phase = VPQ_head_phase;
   while (!(i == VPQ_tail && i_phase == VPQ_tail_phase)) {
      if (VPQ[i].pc == pc) count++;
      i = (i + 1) % qsize;
      if (i == 0) i_phase = !i_phase;
   }
   return count;
}

// Select the best prediction for in-flight instance k.
// Returns true if a confident predictor contributed, pred_val is set accordingly.
// If no predictor is confident, pred_val is set to a best-guess for stats.
static bool select_pred(const H3VP_entry &e, int64_t k,
                         uint64_t conf1, uint64_t conf2, int64_t &pred_val)
{
   // Arithmetic predictor (priority 1): hist0 + k * diff
   if (e.num_ret >= 2) {
      uint64_t thresh = e.failed_arith ? conf2 : conf1;
      if ((uint64_t)e.conf_arith >= thresh) {
         pred_val = e.hist0 + k * e.diff;
         return true;
      }
   }
   // Two-periodic predictor (priority 2): alternates between hist1 and hist0
   //   odd  instance → hist1 (next value in the 2-cycle after hist0)
   //   even instance → hist0 (same as last retired)
   if (e.num_ret >= 2) {
      uint64_t thresh = e.failed_2per ? conf2 : conf1;
      if ((uint64_t)e.conf_2per >= thresh) {
         pred_val = (k % 2 == 1) ? e.hist1 : e.hist0;
         return true;
      }
   }
   // Three-periodic predictor (priority 3): cycles through hist2 → hist1 → hist0
   //   k%3==1 → hist2, k%3==2 → hist1, k%3==0 → hist0
   if (e.num_ret >= 3) {
      uint64_t thresh = e.failed_3per ? conf2 : conf1;
      if ((uint64_t)e.conf_3per >= thresh) {
         int64_t r = k % 3;
         pred_val = (r == 0) ? e.hist0 : (r == 1) ? e.hist2 : e.hist1;
         return true;
      }
   }
   // No confident predictor; provide arithmetic best-guess for measurement stats.
   if (e.num_ret >= 2)
      pred_val = e.hist0 + k * e.diff;
   else
      pred_val = 0;
   return false;
}

int64_t H3VP::install_VPQ(uint64_t pc) {
   int64_t pred_val = 0;

   if (tag_match(pc)) {
      H3VP_entry &e = table[get_index(pc)];
      e.instance++;
      select_pred(e, e.instance, conf1, conf2, pred_val);
   }

   // Always allocate a VPQ entry (structural hazard model requires it).
   VPQ[VPQ_tail].pc  = pc;
   VPQ[VPQ_tail].val = pred_val;

   VPQ_tail = (VPQ_tail + 1) % qsize;
   if (VPQ_tail == 0) VPQ_tail_phase = !VPQ_tail_phase;

   return pred_val;
}

// Returns conf2 (== SVP_CONF_MAX set by caller) when any predictor is speculating;
// returns 0 otherwise.  rename.cc uses (confidence == SVP_CONF_MAX) as the gate.
int H3VP::get_confidence(uint64_t pc) {
   if (!tag_match(pc)) return 0;
   const H3VP_entry &e = table[get_index(pc)];

   if (e.num_ret >= 2 && (uint64_t)e.conf_arith >= (e.failed_arith ? conf2 : conf1))
      return (int)conf2;
   if (e.num_ret >= 2 && (uint64_t)e.conf_2per  >= (e.failed_2per  ? conf2 : conf1))
      return (int)conf2;
   if (e.num_ret >= 3 && (uint64_t)e.conf_3per  >= (e.failed_3per  ? conf2 : conf1))
      return (int)conf2;
   return 0;
}

void H3VP::update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value) {
   VPQ[vpq_index].val = true_value;
}

// Initialize a new history table entry for pc (called on cache miss at retirement).
void H3VP::install_entry(uint64_t pc, int64_t true_value) {
   uint64_t idx = get_index(pc);
   H3VP_entry &e = table[idx];

   // Count in-flight VPQ entries for this PC (including the one being retired).
   int inflight = get_inflight_instances(pc);
   int64_t init_inst = (int64_t)(inflight) - 1;
   if (init_inst < 0) init_inst = 0;

   e.tag       = get_tag(pc);
   e.hist0 = e.hist1 = e.hist2 = true_value;
   e.diff      = 0;
   e.instance  = init_inst;
   e.num_ret   = 1;
   e.conf_arith = e.conf_2per = e.conf_3per = 0;
   e.failed_arith = e.failed_2per = e.failed_3per = false;
}

bool H3VP::retire_VPQ(uint64_t pc, int64_t true_value) {
   // VPQ must not be empty.
   assert(!(VPQ_head == VPQ_tail && VPQ_head_phase == VPQ_tail_phase));

   if (VPQ[VPQ_head].pc != pc) {
      fprintf(stderr,
         "H3VP VPQ PC mismatch: head=%lu stored_pc=0x%lx retire_pc=0x%lx stored_val=%ld true_val=%ld\n",
         VPQ_head, VPQ[VPQ_head].pc, pc, VPQ[VPQ_head].val, true_value);
      assert(0);
   }
   if (VPQ[VPQ_head].val != true_value) {
      fprintf(stderr,
         "H3VP VPQ value mismatch: head=%lu pc=0x%lx stored_val=%ld true_val=%ld"
         " (likely missing update_VPQ call — training with true_value)\n",
         VPQ_head, pc, VPQ[VPQ_head].val, true_value);
      VPQ[VPQ_head].val = true_value;   // repair so training uses correct value
   }

   if (!tag_match(pc)) {
      // Cache miss: initialize a new history table entry.
      install_entry(pc, true_value);
   } else {
      uint64_t idx = get_index(pc);
      H3VP_entry &e = table[idx];

      // --- Train each predictor ---
      // Training rule: compare the current actual value against what the predictor
      // would predict as the "next" value rolling off the current history state.
      // This rolling approach is equivalent to per-allocation tracking for true
      // arithmetic/periodic sequences (proven in project documentation).

      // Arithmetic: consecutive stride must stay constant.
      if (e.num_ret >= 2) {
         int64_t new_diff = true_value - e.hist0;
         if (new_diff == e.diff) {
            if (e.conf_arith < 65535) e.conf_arith++;
         } else {
            e.conf_arith   = 0;
            e.failed_arith = true;
         }
      }

      // Two-periodic: actual must equal hist1 (the value one cycle back).
      if (e.num_ret >= 2) {
         if (true_value == e.hist1) {
            if (e.conf_2per < 65535) e.conf_2per++;
         } else {
            e.conf_2per   = 0;
            e.failed_2per = true;
         }
      }

      // Three-periodic: actual must equal hist2 (the value two cycles back).
      if (e.num_ret >= 3) {
         if (true_value == e.hist2) {
            if (e.conf_3per < 65535) e.conf_3per++;
         } else {
            e.conf_3per   = 0;
            e.failed_3per = true;
         }
      }

      // Update rolling history.
      e.diff  = true_value - e.hist0;
      e.hist2 = e.hist1;
      e.hist1 = e.hist0;
      e.hist0 = true_value;
      if (e.num_ret < 3) e.num_ret++;

      e.instance--;
      assert(e.instance >= 0);
   }

   VPQ_head = (VPQ_head + 1) % qsize;
   if (VPQ_head == 0) VPQ_head_phase = !VPQ_head_phase;
   return true;
}

void H3VP::partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new) {
   while (!(VPQ_tail == VPQ_tail_new && VPQ_tail_phase == VPQ_tail_phase_new)) {
      if (VPQ_tail == 0) {
         VPQ_tail_phase = !VPQ_tail_phase;
         VPQ_tail = qsize - 1;
      } else {
         VPQ_tail--;
      }
      uint64_t rollback_pc = VPQ[VPQ_tail].pc;
      if (tag_match(rollback_pc)) {
         H3VP_entry &e = table[get_index(rollback_pc)];
         if (e.instance > 0) e.instance--;
      }
   }
}

void H3VP::full_rollback() {
   for (auto &e : table) e.instance = 0;
   VPQ_head = VPQ_tail = 0;
   VPQ_head_phase = VPQ_tail_phase = false;
}

// Minimum bits to represent values 0..value (ceiling log2(value+1)).
uint64_t H3VP::get_bits(uint64_t value) const {
   if (value == 0) return 0;
   if (value == 1) return 1;
   uint64_t bits = 0;
   uint64_t i = 1;
   while (i < value) { i <<= 1; bits++; }
   return bits;
}

uint64_t H3VP::get_table_storage_bytes() const {
   // Hardware cost per entry (matching paper's Table II structure, adapted):
   //   tag:          tag_bits
   //   hist0..hist2: 3 × 64 = 192 bits
   //   diff:         64 bits
   //   instance ctr: ceil(log2(qsize+1)) bits
   //   num_ret:      2 bits  (values 0–3)
   //   conf_arith/2per/3per: 3 × ceil(log2(conf2+1)) bits each
   //   failed flags: 3 bits
   uint64_t instance_bits = get_bits(qsize);
   uint64_t conf_bits     = get_bits(conf2);

   uint64_t entry_bits = tag_bits + 192 + 64 + instance_bits + 2 + 3 * conf_bits + 3;
   uint64_t num_entries = 1ULL << index_bits;
   uint64_t total_bits  = num_entries * entry_bits;

   return (total_bits + 7ULL) / 8ULL;
}
