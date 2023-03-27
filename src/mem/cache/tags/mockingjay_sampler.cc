#include "mem/cache/tags/mockingjay_sampler.hh"

namespace gem5 {

constexpr double TEMP_DIFFERENCE = 1.0 / 16.0;
constexpr int MAXRD_THRESHOLD = 22;

uint64_t CRC_HASH( uint64_t _blockAddress ) {
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long _returnVal = _blockAddress;
    for ( unsigned int i = 0; i < 3; i++)
        _returnVal = ( ( _returnVal & 1 ) == 1 ) ? ( ( _returnVal >> 1 ) ^ crcPolynomial ) : ( _returnVal >> 1 );
    return _returnVal;
}

int time_elapsed(int global, int local) {
    if (global >= local) {
        return global - local;
    }
    global = global + (1 << TIMESTAMP_LEN);
    return global - local;
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
    
    
}

uint16_t ReuseDistPredictor::predict(uint64_t PC, bool hit, int core_id, int etr_inf) {
    uint64_t signature = get_pc_signature(PC, hit, false, core_id, _num_cpus) % num_entries;
    DPRINTF(CacheRepl, "Predictor ---- Hashed PC: 0x%.8x", signature);
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

int ReuseDistPredictor::log2_num_entries() {
    return (int) std::log2(num_entries);
}

SampledCache::SampledCache(const int num_sets, const int num_cache_sets, const int cache_block_size, const int timer_size, const int num_cpus)
    : _num_sets(num_sets), _num_cache_sets(num_cache_sets), _cache_block_size(cache_block_size), _timer_size(1 << timer_size), _num_cpus(num_cpus) {
    sample_data = new CacheSet[num_sets];
    set_timestamp_counter = new uint64_t[num_sets];
    for (int i = 0; i < num_sets; i++) {
        set_timestamp_counter[i] = 0;
    }
    _log2_num_sets = (int) std::log2(num_sets);
    _log2_cache_block_size = (int) std::log2(cache_block_size);
}

SampledCache::~SampledCache() {
    delete[] sample_data;
    delete[] set_timestamp_counter;
}

bool SampledCache::sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, bool hit, bool *evict, bool *sampled_hit, int core_id) {
    int log2_num_cache_sets = (int) std::log2(_num_cache_sets);

    if (is_sampled_set(set, log2_num_cache_sets, _log2_num_sets)) {

        DPRINTF(CacheRepl, "Sampler ---- Set hit: Set index %d\n", set);

        uint64_t set_index = (addr >> _log2_cache_block_size) & ((1 << (_log2_num_sets + log2_num_cache_sets)) - 1);
        uint16_t addr_tag = (addr >> (_log2_cache_block_size + _log2_num_sets + log2_num_cache_sets)) & ADDRESS_TAG_MASK;

        // TODO: Core id is not hashed into PC
        uint16_t hashed_pc = get_pc_signature(PC, hit, false, core_id, _num_cpus) & HASHED_PC_MASK;
        uint8_t timestamp = set_timestamp_counter[set_index];

        DPRINTF(CacheRepl, "Sampler ---- Set info: Set index %d, Address Tag: 0x%.8x, Hased PC: 0x%.8x, Current Timestamp: %d\n", set, addr_tag, hashed_pc, timestamp);

        *curr_timestamp = timestamp;

        if (!sample_data[set_index].access(addr_tag, hashed_pc, timestamp, last_PC, last_timestamp)) {
            set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
            *evict = sample_data[set_index].insert(addr_tag, hashed_pc, timestamp, last_timestamp, last_PC);
            *sampled_hit = false;
            DPRINTF(CacheRepl, "Sampler ---- Sampler miss handling: Last timestamp: %d, Current Timestamp: %d\n", last_timestamp, set_timestamp_counter[set_index]);
        } else {
            set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
            *evict = false;
            *sampled_hit = true;
            DPRINTF(CacheRepl, "Sampler ---- Sampler hit: Last timestamp: %d, Current Timestamp: %d\n", last_timestamp, set_timestamp_counter[set_index]);
        }
        return true;
    } else {
        return false;
    }
}

}