#include "mem/cache/tags/mockingjay_sampler.hh"

namespace gem5 {

static constexpr double TEMP_DIFFERENCE = 1.0 / 16.0;
static constexpr int MAXRD_THRESHOLD = 22;
static constexpr int INTERNAL_SAMPLED_BITS = 4;

uint64_t CRC_HASH( uint64_t _blockAddress ) {
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long _returnVal = _blockAddress;
    for ( unsigned int i = 0; i < 3; i++)
        _returnVal = ( ( _returnVal & 1 ) == 1 ) ? ( ( _returnVal >> 1 ) ^ crcPolynomial ) : ( _returnVal >> 1 );
    return _returnVal;
}


int temporal_difference(int init, int sample, int inf_rd) {
    if (sample > init) {
        int diff = sample - init;
        diff = diff * TEMP_DIFFERENCE;
        diff = std::min(1, diff);
        return std::min(init + diff, inf_rd);
    } else if (sample < init) {
        int diff = init - sample;
        diff = diff * TEMP_DIFFERENCE;
        diff = std::min(1, diff);
        return std::max(init - diff, 0);
    } else {
        return init;
    }
}

bool is_sampled_set(int set, int log2_cache_sets, int log2_sampled_sets) {
    int mask_length = log2_cache_sets - log2_sampled_sets;
    int mask = (1 << mask_length) - 1;
    return (set & mask) == ((set >> (log2_cache_sets - mask_length)) & mask);
}

uint64_t get_pc_signature(uint64_t pc, bool hit, bool prefetch, uint32_t core, uint32_t num_cpu) {
    if (num_cpu == 1) {
        pc = pc << 1;
        if (hit) {
            pc = pc | 1;
        }
        pc = pc << 1;
        if (prefetch) {
            pc = pc | 1;
        }
        pc = CRC_HASH(pc);
        pc = (pc << (64 - HASHED_PC_LEN)) >> (64 - HASHED_PC_LEN);
    } else {
        pc = pc << 1;
        if (prefetch) {
            pc = pc | 1;
        }
        pc = pc << 2;
        pc = pc | core;
        pc = CRC_HASH(pc);
        pc = (pc << (64 - HASHED_PC_LEN)) >> (64 - HASHED_PC_LEN);
    }
    return pc;
}

ReuseDistPredictor::ReuseDistPredictor(const int num_entries, const int bits_per_entry, const int aging_clock_size, const int num_cpus) : 
                                       num_entries(num_entries), bits_per_entry(bits_per_entry), _granularity(aging_clock_size), _num_cpus(num_cpus) {
    counters = new int[num_entries];
    for (int i = 0; i < num_entries; i++) {
        counters[i] = -1;
    }
    max_value = (int) (std::pow(2, bits_per_entry) - 1);
    max_rd = max_value - MAXRD_THRESHOLD;
}

ReuseDistPredictor::~ReuseDistPredictor() {
    delete[] counters;
}

void ReuseDistPredictor::train(uint64_t last_PC, bool sampled_cache_hit, uint8_t curr_timestamp, uint8_t last_timestamp, bool evict) {
    // TODO: Mockingjay train mechanism
    // Sampled cache hit
    //  1. If it first-time train, then use difference directly
    //  2. If it is not first-time train, use temporal difference
    // Sampled cache miss
    //  1. Train as scan (INF_RD)
    if (sampled_cache_hit) {
        DPRINTF(MockingjayDebug, "Predictor (train) ---- Sample cached hit: Last signature %ld, Current timestamp: %d, Last Timestamp: %d\n", 
                last_PC, curr_timestamp, last_timestamp);
        int sample = time_elapsed(curr_timestamp, last_timestamp);
        if (sample <= max_value) {
            if (counters[last_PC] == -1) {
                counters[last_PC] = sample;
                DPRINTF(MockingjayDebug, "Predictor (hit) ---- Uninitialized: sample: %d\n", counters[last_PC]);
            } else {
                int debug_old_pred_value = counters[last_PC];
                counters[last_PC] = temporal_difference(counters[last_PC], sample, max_value);
                DPRINTF(MockingjayDebug, "Predictor (hit) ---- Old train value: %d, New train value: sample: %d\n", debug_old_pred_value, counters[last_PC]);
            }
        }
    } else {
        if (evict) {
            DPRINTF(MockingjayDebug, "Predictor (train) ---- Sample cached miss and eviction: Last signature %ld, Current timestamp: %d, Last Timestamp: %d\n", 
                last_PC, curr_timestamp, last_timestamp);
            if (counters[last_PC] == -1) {
                counters[last_PC] = max_value;
                DPRINTF(MockingjayDebug, "Predictor (miss and eviction) ---- Uninitialized: sample: %d\n", counters[last_PC]);
            } else {
                int debug_old_pred_value = counters[last_PC];
                counters[last_PC] = std::min(counters[last_PC] + 1, max_value);
                DPRINTF(MockingjayDebug, "Predictor (miss and eviction) ---- Old train value: %d, New train value: sample: %d\n", debug_old_pred_value, counters[last_PC]);

            }
        }
    }
}

uint16_t ReuseDistPredictor::predict(uint64_t PC, bool hit, int core_id, int etr_inf) {
    uint64_t signature = get_pc_signature(PC, hit, false, core_id, _num_cpus) % num_entries;
    DPRINTF(MockingjayDebug, "Predictor (predict) ---- Hashed PC: 0x%.8x\n", signature);
    if (counters[signature] == -1) {
        // Initialization of reuse predictor
        if (_num_cpus == 1) {
            return 0;
        } else {
            return etr_inf;
        }
    } else {
        // Post-initialization
        if (counters[signature] > max_rd) {
            return etr_inf;
        } else {
            return counters[signature] / _granularity;
        }
    }
}

bool ReuseDistPredictor::bypass(uint64_t PC, uint8_t max_etr, bool hit, int core_id, bool cache_line_valid) {
    if (cache_line_valid) {
        uint64_t signature = get_pc_signature(PC, hit, false, core_id, _num_cpus) % num_entries;
        if ((counters[signature] > max_rd || ((counters[signature] / _granularity) > max_etr)) && counters[signature] != -1) {
            DPRINTF(MockingjayDebug, "Predictor (bypass) ---- Hashed PC: 0x%.8x, Counters: %d, MAX_RD: %d, MAX_ETR: %d\n", signature, counters[signature], max_rd, max_etr);
            return true;
        } else {
            return false;
        }
    } else {
        return false;
    }
}

int ReuseDistPredictor::getLog2NumEntries() {
    return (int) std::log2(num_entries);
}


int ReuseDistPredictor::getInfRd() {
    return max_value;
}

SampledCache::SampledCache(const int num_sampled_sets, const int num_cache_sets, const int cache_block_size, const int timer_size, 
                           const int num_cpus, const int num_sampled_internal_sets)
    : _num_sampled_sets(num_sampled_sets), _num_cache_sets(num_cache_sets), _cache_block_size(cache_block_size), _timer_size(1 << timer_size), 
        _num_cpus(num_cpus), _num_sampled_internal_sets(num_sampled_internal_sets) {
    sample_data = new CacheSet[num_sampled_sets];
    set_timestamp_counter = new uint64_t[num_sampled_sets];
    for (int i = 0; i < num_sampled_sets; i++) {
        set_timestamp_counter[i] = 0;
    }
    _log2_num_sampled_sets = (int) std::log2(num_sampled_sets);
    _log2_cache_block_size = (int) std::log2(cache_block_size);
    _log2_sampled_internal_sets = (int) std::log2(num_sampled_internal_sets);
}

SampledCache::~SampledCache() {
    delete[] sample_data;
    delete[] set_timestamp_counter;
}

bool SampledCache:: sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, bool hit, bool *evict, bool *sampled_hit, int core_id, uint64_t inf_rd) {
    int log2_num_cache_sets = (int) std::log2(_num_cache_sets);
    int log2_num_sets = _log2_num_sampled_sets - _log2_sampled_internal_sets;
    uint64_t num_sets_mask = (1 << log2_num_sets) - 1;
    uint64_t num_sampled_internal_sets_mask = (1 << _log2_sampled_internal_sets) - 1;

    if (is_sampled_set(set, log2_num_cache_sets, log2_num_sets)) {
        
        DPRINTF(MockingjayDebug, "Sampler ---- Set hit: Cache Set index %d\n", set);

        uint16_t addr_tag = (addr >> (_log2_cache_block_size + _log2_sampled_internal_sets + log2_num_cache_sets)) & ADDRESS_TAG_MASK;

        uint16_t addr_tag_to_set = (addr >> (_log2_cache_block_size + log2_num_cache_sets)) & num_sampled_internal_sets_mask;
        uint64_t set_index = (addr_tag_to_set << log2_num_sets) | (set & num_sets_mask);
        gem5_assert(set_index < _num_sampled_sets, "Set index should be within sampled set entries, Index: %d", set_index);

        uint16_t hashed_pc = get_pc_signature(PC, hit, false, core_id, _num_cpus) & HASHED_PC_MASK;
        uint8_t timestamp = set_timestamp_counter[set_index];

        DPRINTF(MockingjayDebug, "Sampler ---- Set info: Set index %d, Address Tag: 0x%.8x, Hased PC: 0x%.8x, Current Timestamp: %d\n", set_index, addr_tag, hashed_pc, timestamp);

        *curr_timestamp = timestamp;

        if (!sample_data[set_index].access(addr_tag, hashed_pc, timestamp, last_PC, last_timestamp)) {
            set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
            *evict = sample_data[set_index].insert(addr_tag, hashed_pc, timestamp, last_timestamp, last_PC, inf_rd);
            *sampled_hit = false;
            DPRINTF(MockingjayDebug, "Sampler ---- Sampler miss handling: Last timestamp: %d, Current Timestamp: %d\n", *last_timestamp, set_timestamp_counter[set_index]);
        } else {
            set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
            *evict = false;
            *sampled_hit = true;
            DPRINTF(MockingjayDebug, "Sampler ---- Sampler hit: Last timestamp: %d, Current Timestamp: %d\n", *last_timestamp, set_timestamp_counter[set_index]);
        }
        return true;
    } else {
        return false;
    }
}

}