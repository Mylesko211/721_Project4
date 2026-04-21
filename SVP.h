#include <cstdint>
#include <vector>
#include <iostream>
#include <cmath>

using namespace std;

struct VPQ_entry{
    uint64_t pc;
    int64_t val;
    bool valid;
    bool phase;
};

struct SVP_entry {
    uint64_t tag;
    int64_t last_value;
    int64_t stride;
    uint64_t confidence;
    int64_t instance;

    bool valid;
};

struct checkpoint_entry {
    unsigned int branch_mask;
    int64_t VPQ_tail;
    bool VPQ_tail_phase;
    uint64_t branch_id;

    uint64_t pc;
};

class SVP {
public:
    // params
    uint64_t qsize;
    bool oracleconf;
    uint64_t index_bits;
    uint64_t tag_bits;
    uint64_t confmax;

    bool perfect_value_pred; // if true, the predictor always predicts correctly.

    bool pred_int_alu;
    bool pred_fp_alu;
    bool pred_load;

    

    uint64_t VPQ_head;
    uint64_t VPQ_tail;
    bool VPQ_head_phase;
    bool VPQ_tail_phase;
    vector<VPQ_entry> VPQ;

    vector<checkpoint_entry> checkpoints;


    vector<SVP_entry> SVP_table;

    SVP(uint64_t qsize, bool oracleconf, uint64_t index_bits, uint64_t tag_bits, uint64_t confmax, bool perfect_value_pred, bool pred_int_alu, bool pred_fp_alu, bool pred_load);
    ~SVP();

    bool is_eligible(bool is_int_alu, bool is_fp_alu, bool is_load);

    int get_instances(uint64_t pc);
    int get_confidence(uint64_t pc);
    int get_VPQ_free_entries();

    bool predict(uint64_t pc, int64_t &pred_value, uint64_t &confidence);
    bool probe(uint64_t pc, int64_t &pred_value, uint64_t &confidence);

    void update_SVP(uint64_t pc);
    void update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value);

    bool install_SVP(uint64_t pc, int64_t true_value);
    int64_t install_VPQ(uint64_t pc);
    
    bool is_VPQ_full();
    bool retire_VPQ(uint64_t pc);

    void partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new);
    void full_rollback();

    bool search(uint64_t pc);
    
    uint64_t get_table_storage_bytes() const;

    void checkpoint(uint64_t &tail, bool &phase);
};
