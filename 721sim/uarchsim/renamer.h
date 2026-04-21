#include <inttypes.h>

class renamer {
//private:
public:

    uint64_t n_log_regs;
    uint64_t n_phys_regs;
    uint64_t n_branches;
    uint64_t n_active;

    uint64_t *RMT;

    uint64_t *AMT;

    uint64_t *FreeList;
    uint64_t FL_head;
    uint64_t FL_tail;
    bool FL_head_phase;
    bool FL_tail_phase;
    uint64_t FL_size;

    struct AL_entry {
        bool dest_valid;
        uint64_t log_reg;
        uint64_t phys_reg;
        bool completed;
        bool exception;
        bool load_viol;
        bool br_misp;
        bool val_misp;
        bool load;
        bool store;
        bool branch;
        bool amo;
        bool csr;
        uint64_t PC;
    };
    AL_entry *ActiveList;
    uint64_t AL_head;
    uint64_t AL_tail;
    bool AL_head_phase;
    bool AL_tail_phase;
    uint64_t AL_size;

    uint64_t *PRF;

    bool *PRF_ready;

	uint64_t GBM;

    struct Checkpoint_entry {
        uint64_t *shadow_RMT;
        uint64_t FL_head;
        bool FL_head_phase;
        uint64_t GBM;
    };
    Checkpoint_entry *Checkpoints;

// public:
	renamer(uint64_t n_log_regs,
		uint64_t n_phys_regs,
		uint64_t n_branches,
		uint64_t n_active);

	~renamer();

	bool stall_reg(uint64_t bundle_dst);

	bool stall_branch(uint64_t bundle_branch);

	uint64_t get_branch_mask();

    int get_free_phys_regs();

    int get_free_checkpoints();

	uint64_t rename_rsrc(uint64_t log_reg);

	uint64_t rename_rdst(uint64_t log_reg);

	uint64_t checkpoint();

	bool stall_dispatch(uint64_t bundle_inst);

	uint64_t dispatch_inst(bool dest_valid,
	                       uint64_t log_reg,
	                       uint64_t phys_reg,
	                       bool load,
	                       bool store,
	                       bool branch,
	                       bool amo,
	                       bool csr,
	                       uint64_t PC);

	bool is_ready(uint64_t phys_reg);

	void clear_ready(uint64_t phys_reg);

	uint64_t read(uint64_t phys_reg);

	void set_ready(uint64_t phys_reg);

	void write(uint64_t phys_reg, uint64_t value);

	void set_complete(uint64_t AL_index);

	void resolve(uint64_t AL_index,
		     uint64_t branch_ID,
		     bool correct);

	bool precommit(bool &completed,
                       bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
	               bool &load, bool &store, bool &branch, bool &amo, bool &csr,
		       uint64_t &PC);

	void commit();

	void squash();

	void set_exception(uint64_t AL_index);
	void set_load_violation(uint64_t AL_index);
	void set_branch_misprediction(uint64_t AL_index);
	void set_value_misprediction(uint64_t AL_index);

	bool get_exception(uint64_t AL_index);
};
