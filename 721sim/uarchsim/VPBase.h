#pragma once
#include <cstdint>

// Abstract base for value predictors – all pipeline stages use this interface.
class VPBase {
public:
   virtual ~VPBase() {}

   virtual bool is_eligible(bool is_int_alu, bool is_fp_alu, bool is_load) = 0;
   virtual int  get_VPQ_free_entries() = 0;
   virtual void checkpoint(uint64_t &tail, bool &phase) = 0;
   virtual int64_t install_VPQ(uint64_t pc) = 0;
   virtual bool search(uint64_t pc) = 0;
   virtual int  get_confidence(uint64_t pc) = 0;
   virtual void update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value) = 0;
   virtual bool retire_VPQ(uint64_t pc, int64_t true_value) = 0;
   virtual void partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new) = 0;
   virtual void full_rollback() = 0;
   virtual uint64_t get_table_storage_bytes() const = 0;
};
