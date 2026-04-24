#include "SVP.h"
#include "assert.h"

SVP::SVP(uint64_t qsize, bool oracleconf, uint64_t index_bits, uint64_t tag_bits, uint64_t confmax, bool perfect_value_pred, bool pred_int_alu, bool pred_fp_alu, bool pred_load) {
   this->qsize = qsize;
   this->oracleconf = oracleconf;
   this->index_bits = index_bits;
   this->tag_bits = tag_bits;
   this->confmax = confmax;
   this->perfect_value_pred = perfect_value_pred;
   this->pred_int_alu = pred_int_alu;
   this->pred_fp_alu = pred_fp_alu;
   this->pred_load = pred_load;

   this->VPQ_head = 0;
   this->VPQ_tail = 0;

   this->VPQ_head_phase = false;
   this->VPQ_tail_phase = false;

   this->index_mask = ((1ULL << index_bits) - 1ULL) << 2;
   this->tag_mask = ((1ULL << tag_bits) - 1ULL) << (2 + index_bits);

   VPQ.resize(qsize);
   SVP_table.resize(1ULL << index_bits);

   for (auto &entry : SVP_table) {
      entry.tag = 0;
      entry.last_value = 0;
      entry.stride = 0;
      entry.confidence = 0;
      entry.instance = 0;

   }

   for (auto &entry : VPQ) {
      entry.pc = 0;
      entry.val = 0;
   }
}

SVP::~SVP() {
}

bool SVP::is_eligible(bool is_int_alu, bool is_fp_alu, bool is_load) {
   if ((is_int_alu && pred_int_alu) || (is_fp_alu && pred_fp_alu) || (is_load && pred_load)) {
      return true;
   }
   return false;
}

bool SVP::is_VPQ_full() {
   return (VPQ_tail == VPQ_head && VPQ_tail_phase != VPQ_head_phase);
}

int SVP::get_instances(uint64_t pc) {
   int instances = 0;
   uint64_t index = get_index(pc);
   if (search(pc)) {
      instances = (int) SVP_table[index].instance;
   }
   return instances;
}

int SVP::get_confidence(uint64_t pc) {
   int confidence = 0;
   uint64_t index = get_index(pc);
   if (search(pc)) {
      confidence = (int) SVP_table[index].confidence;
   }
   return confidence;
}

int SVP::get_inflight_instances(uint64_t pc) {
   
   int i = VPQ_head;
   bool i_phase = VPQ_head_phase;
   
   int inflight = 0;
   while (!(i == VPQ_tail && i_phase == VPQ_tail_phase)) {
      if (VPQ[i].pc == pc) inflight++;
      i = (i + 1) % qsize;
      if (i == 0) i_phase = !i_phase;
   }
   return inflight;
}

int SVP::get_VPQ_free_entries() {
   if (VPQ_head == VPQ_tail && VPQ_head_phase != VPQ_tail_phase) {
      return 0;
   } else if (VPQ_head == VPQ_tail && VPQ_head_phase == VPQ_tail_phase) {
      return (int) qsize;
   } else if (VPQ_tail > VPQ_head && VPQ_tail_phase == VPQ_head_phase) {
      return (int) (qsize - (VPQ_tail - VPQ_head));
   } else {
      return (int) (VPQ_head - VPQ_tail);
   }
}

void SVP::update_SVP(uint64_t pc) {
   uint64_t index = get_index(pc);

   int64_t new_stride = VPQ[VPQ_head].val - SVP_table[index].last_value;
   if (new_stride == SVP_table[index].stride) {
      if (SVP_table[index].confidence < confmax) SVP_table[index].confidence++;
   } else {
      SVP_table[index].confidence = 0;
      SVP_table[index].stride = new_stride;
   }
   SVP_table[index].last_value = VPQ[VPQ_head].val;
}

void SVP::update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value) {
   VPQ[vpq_index].val = true_value;
}

bool SVP::install_SVP(uint64_t pc, int64_t true_value) {
   uint64_t index = get_index(pc);

   int inflight_same = get_inflight_instances(pc);
   int64_t init_inst = (int64_t) inflight_same - 1;
   if (init_inst < 0)
      init_inst = 0;

   SVP_entry new_entry{};
   new_entry.tag = get_tag(pc);
   new_entry.confidence = 0;

   new_entry.last_value = true_value;
   new_entry.stride =  true_value;
   new_entry.instance = init_inst;

   SVP_table[index] = new_entry;
   return true;
}

int64_t SVP::install_VPQ(uint64_t pc) {
   int64_t pred_val = 0;
   uint64_t index = get_index(pc);

   if (search(pc)) {
      SVP_table[index].instance++;
      pred_val = SVP_table[index].last_value + SVP_table[index].stride * SVP_table[index].instance;
      assert(SVP_table[index].instance >= 0);
   }

   VPQ[VPQ_tail].pc = pc;
   VPQ[VPQ_tail].val = pred_val;

   VPQ_tail = (VPQ_tail + 1) % qsize;
   if (VPQ_tail == 0) {
      VPQ_tail_phase = !VPQ_tail_phase;
   }
   return pred_val;
}

bool SVP::retire_VPQ(uint64_t pc, int64_t true_value) {
   assert(VPQ[VPQ_head].pc == pc);
   assert(VPQ[VPQ_head].val == true_value);
   uint64_t index = get_index(pc);
   // if the VPQ is empty return false
   if (VPQ_head == VPQ_tail && VPQ_head_phase == VPQ_tail_phase) assert(0);
   // if the head of the VPQ is the same as the pc return true
   //if (get_tag(VPQ[VPQ_head].pc) == get_tag(pc) && get_index(VPQ[VPQ_head].pc) == get_index(pc)){
      if (search(VPQ[VPQ_head].pc)) {
         update_SVP(VPQ[VPQ_head].pc);
         SVP_table[index].instance--;
         assert(SVP_table[index].instance >= 0);
      } else {
         install_SVP(VPQ[VPQ_head].pc, VPQ[VPQ_head].val);
      }
      VPQ_head = (VPQ_head + 1) % qsize;
      if (VPQ_head == 0) {
         VPQ_head_phase = !VPQ_head_phase;
      }
      return true;
}

void SVP::partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new) {
   while (!(VPQ_tail == VPQ_tail_new && VPQ_tail_phase == VPQ_tail_phase_new)) {
      if (VPQ_tail == 0) {
         VPQ_tail_phase = !VPQ_tail_phase;
         VPQ_tail = qsize - 1;
      } else
         VPQ_tail = VPQ_tail - 1;

      uint64_t rollback_pc = VPQ[VPQ_tail].pc;
      uint64_t index = get_index(rollback_pc);
      if (search(rollback_pc)) {
         SVP_table[index].instance--;
      }
   }
   return;
}

void SVP::full_rollback() {
   for (uint64_t i = 0; i < (1ULL << index_bits); i++) {
      SVP_table[i].instance = 0;
   }
   VPQ_head = 0;
   VPQ_head_phase = false;
   VPQ_tail = 0;
   VPQ_tail_phase = false;
   return;
}

bool SVP::search(uint64_t pc) {
   uint64_t index = get_index(pc);
   assert(index < (1ULL << index_bits));

   if (tag_bits == 0) return true;

   return ((SVP_table[index].tag == get_tag(pc)));
}

uint64_t SVP::get_table_storage_bytes() const {
   auto bits_for_unsigned = [](uint64_t max_value) -> uint64_t {
      uint64_t bits = 0;
      do {
         bits++;
         max_value >>= 1;
      } while (max_value);
      return bits;
   };

   uint64_t confidence_bits = bits_for_unsigned(confmax);
   uint64_t instance_bits = bits_for_unsigned(qsize);
   uint64_t entry_bits = tag_bits + 64 + 64 + confidence_bits + instance_bits;
   uint64_t table_entries = (1ULL << index_bits);
   uint64_t total_bits = table_entries * entry_bits;

   return ((total_bits + 7ULL) / 8ULL);
}

void SVP::checkpoint(uint64_t &tail, bool &phase) {
   tail = VPQ_tail;
   phase = VPQ_tail_phase;
}

uint64_t SVP::get_index(uint64_t pc) {
   return ((pc & this->index_mask) >> 2);
}

uint64_t SVP::get_tag(uint64_t pc) {
   return ((pc & this->tag_mask) >> (2 + this->index_bits));
}

uint64_t SVP::get_bits(uint64_t value) {
   if (value == 0) return 0;
   if (value == 1) return 1;
   uint64_t bits = 0;
   int i = 1;
   while (i < value) {
      i <<= 1;
      bits++;
   }
   return bits;
}