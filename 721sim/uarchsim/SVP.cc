#include "SVP.h"

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

    
    VPQ.resize(qsize);
    SVP_table.resize(1ULL << index_bits);

    for (auto &entry: SVP_table) {
        entry.tag = 0;
        entry.last_value = 0;
        entry.stride = 0;
        entry.confidence = 0;
        entry.instance = 0;
        entry.valid = true;
    }

    for (auto &entry : VPQ) {
        entry.pc = 0;
        entry.val = 0;
        entry.valid = false;
        entry.phase = false;
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
    int index = (pc >> 2) & ((1ULL << index_bits) - 1ULL);
    if(search(pc) && SVP_table[index].valid) {
        instances = SVP_table[index].instance;
    }
    return instances;
}

int SVP::get_confidence(uint64_t pc) {
    int confidence = 0;
    int index = (pc >> 2) & ((1ULL << index_bits) - 1ULL);
    if(search(pc) && SVP_table[index].valid) {
        confidence = SVP_table[index].confidence;
    }
    return confidence;
}

int SVP::get_VPQ_free_entries() {
    if (VPQ_head == VPQ_tail && VPQ_head_phase != VPQ_tail_phase) {
        return 0;
    }
    else if (VPQ_head == VPQ_tail && VPQ_head_phase == VPQ_tail_phase) {
        return qsize;
    }
    else if (VPQ_tail > VPQ_head && VPQ_tail_phase == VPQ_head_phase) {
        return qsize - (VPQ_tail - VPQ_head);
    }
    else {
        return VPQ_head - VPQ_tail;
    }
}

bool SVP::predict(uint64_t pc, int64_t &pred_value, uint64_t &confidence) {
    uint64_t raw_conf = 0;
    if (!probe(pc, pred_value, raw_conf))
        return false;

    bool high_conf = oracleconf || (raw_conf == confmax);
    if (!high_conf)
        return false;

    confidence = confmax;
    return true;
}

bool SVP::probe(uint64_t pc, int64_t &pred_value, uint64_t &confidence) {
    int index = (pc >> 2) & ((1ULL << index_bits) - 1);
    if (!search(pc)) return false;

    confidence = SVP_table[index].confidence;
    pred_value = (int64_t) SVP_table[index].last_value + ((int64_t) SVP_table[index].stride * (int64_t) (SVP_table[index].instance));
    return true;
}

void SVP::update_SVP(uint64_t pc) {
    int index = (pc >> 2) & ((1ULL << index_bits) - 1);
    int64_t new_stride = VPQ[VPQ_head].val - SVP_table[index].last_value;
    if (new_stride == SVP_table[index].stride) {
        if (SVP_table[index].confidence < confmax) SVP_table[index].confidence++;
    }
    else {
        SVP_table[index].confidence = 0;
        SVP_table[index].stride = new_stride;
    }
    SVP_table[index].last_value = VPQ[VPQ_head].val;
    
}

void SVP::update_VPQ(uint64_t vpq_index, bool vpq_phase, uint64_t pc, int64_t true_value) {
    VPQ[vpq_index].val = true_value;
}

bool SVP::install_SVP(uint64_t pc, int64_t true_value) {
    int i = VPQ_head;
    bool i_phase = VPQ_head_phase;

    int index = (pc >> 2) & ((1ULL << index_bits) - 1);

    
    uint64_t full_tag = pc >> (2 + index_bits);
    uint64_t masked_tag;
    if (tag_bits == 0) masked_tag = 0;
    else if (tag_bits >= 64) masked_tag = full_tag;
    else masked_tag = full_tag & ((1ULL << tag_bits) - 1ULL);

    SVP_entry new_entry;
    new_entry.valid = true;
    new_entry.tag = masked_tag;
    new_entry.last_value = true_value;
    new_entry.stride = 0;
    new_entry.confidence = 0;
    new_entry.instance = 0;
        
        
    while(!(i == VPQ_tail && i_phase == VPQ_tail_phase)) {
        if (VPQ[i].pc == pc) {
            new_entry.instance++;
        }
        i = (i + 1) % qsize;
        if (i == 0) i_phase = !i_phase;
   }

    SVP_table[index] = new_entry;
    return true;
}


int64_t SVP::install_VPQ(uint64_t pc) {
    int64_t pred_val = 0;

    if (search(pc)) {
        uint64_t index = (pc >> 2) & ((1ULL << index_bits) - 1ULL);
        SVP_table[index].instance++;
        pred_val = SVP_table[index].last_value + (SVP_table[index].stride * SVP_table[index].instance);
    }
    else {
        pred_val = 0;
    }
    VPQ[VPQ_tail].pc = pc;
    VPQ[VPQ_tail].val = pred_val;
    VPQ[VPQ_tail].valid = true;
    VPQ[VPQ_tail].phase = VPQ_tail_phase;

    VPQ_tail = (VPQ_tail + 1) % qsize;
    if(VPQ_tail == 0) {
        VPQ_tail_phase = !VPQ_tail_phase;
    }
    return pred_val;
}

bool SVP::retire_VPQ(uint64_t pc) {

    if (VPQ_head == VPQ_tail && VPQ_head_phase == VPQ_tail_phase) return false;
    uint64_t index = (VPQ[VPQ_head].pc >> 2) & ((1ULL << index_bits) - 1ULL);

        if (search(VPQ[VPQ_head].pc)) {
            SVP_table[index].instance--;
            update_SVP(VPQ[VPQ_head].pc);
        }
        else {
            install_SVP(VPQ[VPQ_head].pc, VPQ[VPQ_head].val);
        }

    VPQ[VPQ_head].valid = false;

    VPQ_head = (VPQ_head + 1) % qsize;
    if(VPQ_head == 0) {
        VPQ_head_phase = !VPQ_head_phase;
    }
    return true;
}

void SVP::partial_rollback(uint64_t VPQ_tail_new, bool VPQ_tail_phase_new) {
    uint64_t index;
    while (!(VPQ_tail == VPQ_tail_new && VPQ_tail_phase == VPQ_tail_phase_new)) {
        if(VPQ_tail == 0) {
            VPQ_tail_phase = !VPQ_tail_phase;
            VPQ_tail = qsize - 1;
        }
        else VPQ_tail = VPQ_tail - 1;

        index = (VPQ[VPQ_tail].pc >> 2) & ((1ULL << index_bits) - 1ULL);
        if (search(VPQ[VPQ_tail].pc)) {
            SVP_table[index].instance--;
        }
    }

    index = (VPQ[VPQ_tail].pc >> 2) & ((1ULL << index_bits) - 1ULL);
    if (search(VPQ[VPQ_tail].pc)) {
            SVP_table[index].instance--;
    }

    return;
}

void SVP::full_rollback() {

    /*
    while (!(VPQ_tail == VPQ_head && VPQ_tail_phase == VPQ_head_phase)) {
        if(VPQ_tail == 0) {
            VPQ_tail_phase = !VPQ_tail_phase;
            VPQ_tail = qsize - 1;
        }
        else VPQ_tail = VPQ_tail - 1;
        uint64_t index = (VPQ[VPQ_tail].pc >> 2) & ((1ULL << index_bits) - 1ULL);
        if (search(VPQ[VPQ_tail].pc)) {
            SVP_table[index].instance--;
        }
    }
    */

    for (int i = 0; i < qsize; i++) {
        SVP_table[i].instance = 0;
    }
    VPQ_head = 0;
    VPQ_tail = 0;
    VPQ_head_phase = false;
    VPQ_tail_phase = false;

    return;
}

bool SVP::search(uint64_t pc) {
    int index = (pc >> 2) & ((1ULL << index_bits) - 1ULL);

    if (tag_bits == 0) return true;

    uint64_t full_tag = pc >> (2 + index_bits);
    uint64_t tag;
    if (tag_bits >= 64) tag = full_tag;
    else tag = full_tag & ((1ULL << tag_bits) - 1ULL);

    return (SVP_table[index].tag == tag);
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


