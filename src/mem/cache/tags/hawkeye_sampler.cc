#include "mem/cache/tags/hawkeye_sampler.hh"

#include <cassert>
#include <cmath>

namespace gem5
{

// Sample 64 sets
#define bitmask(l) (((l) == 64) ? (unsigned long long)(-1LL) : ((1LL << (l))-1LL))
#define bits(x, i, l) (((x) >> (i)) & bitmask(l))
#define SAMPLED_SET(set, cache_set) (bits(set, 0 , 6) == bits(set, ((unsigned long long)log2(cache_set) - 6), 6) )


uint64_t CRC( uint64_t _blockAddress ) {
    static const unsigned long long crcPolynomial = 3988292384ULL;
    unsigned long long _returnVal = _blockAddress;
    for ( unsigned int i = 0; i < 32; i++ )
        _returnVal = ( ( _returnVal & 1 ) == 1 ) ? ( ( _returnVal >> 1 ) ^ crcPolynomial ) : ( _returnVal >> 1 );
    return _returnVal;
}

OccupencyVector::OccupencyVector(const uint64_t cache_size, const uint64_t capacity): num_cache(0), num_dont_cache(0), access(0), CACHE_SIZE(cache_size), vector_size(capacity) {
    liveness_history.resize(capacity, 0);
}

void OccupencyVector::add_access(uint64_t curr_quanta) {
    access++;
    liveness_history[curr_quanta] = 0;
}

uint64_t OccupencyVector::get_vector_size() {
    return vector_size;
}

void OccupencyVector::add_prefetch(uint64_t curr_quanta) {
    liveness_history[curr_quanta] = 0;
}

bool OccupencyVector::should_cache(uint64_t curr_quanta, uint64_t last_quanta) {
    bool is_cache = true;

    unsigned int i = last_quanta;
    while (i != curr_quanta)
    {
        if (liveness_history[i] >= CACHE_SIZE)
        {
            is_cache = false;
            break;
        }

        i = (i+1) % liveness_history.size();
    }


    //if ((is_cache) && (last_quanta != curr_quanta))
    if ((is_cache))
    {
        i = last_quanta;
        while (i != curr_quanta)
        {
            liveness_history[i]++;
            i = (i+1) % liveness_history.size();
        }
        assert(i == curr_quanta);
    }

    if (is_cache) num_cache++;
    else num_dont_cache++;

    return is_cache;
}

uint64_t OccupencyVector::get_num_opt_hits() {
    return num_cache;

    // uint64_t num_opt_misses = access - num_cache;
    // return num_opt_misses;
}

PCBasedPredictor::PCBasedPredictor(const int num_entries, const int bits_per_entry): num_entries(num_entries), bits_per_entry(bits_per_entry) {
    counters = new int[num_entries];
    max_value = (int) (std::pow(2, bits_per_entry) - 1);
}

PCBasedPredictor::~PCBasedPredictor() {
    delete[] counters;
}

void PCBasedPredictor::train(uint64_t last_PC, bool opt_decision) {
    uint64_t signature = last_PC % num_entries;
    if (opt_decision) {
        // Cache hit
        if (counters[signature] < max_value) {
            counters[signature] += 1;
        }
    } else {
        if (counters[signature] > 0) {
            counters[signature] -= 1;
        }
    }
}

bool PCBasedPredictor::predict(uint64_t PC) {
    uint64_t signature = CRC(PC) % num_entries;
    return (counters[signature] >> (bits_per_entry - 1)) & 0x1;
}

int PCBasedPredictor::log2_num_entries() {
    return (int) std::log2(num_entries);
}

HistorySampler::HistorySampler(const int num_sets, const int num_cache_sets, const int cache_block_size, const int timer_size)
    : _num_sets(num_sets), _num_cache_sets(num_cache_sets), _cache_block_size(cache_block_size), _timer_size(timer_size) {
    sample_data = new CacheSet[num_sets];
    set_timestamp_counter = new uint64_t[num_sets];
    _log2_num_sets = (int) std::log2(num_sets);
    _log2_cache_block_size = (int) std::log2(cache_block_size);
}

HistorySampler::~HistorySampler() {
    delete[] sample_data;
    delete[] set_timestamp_counter;
}

bool HistorySampler::sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, int log2_num_pred_entries) {
    if (SAMPLED_SET(set, _num_cache_sets)) {

        uint32_t set_index = (addr >> _log2_cache_block_size) % _num_sets;
        uint16_t addr_tag = CRC(addr >> (_log2_cache_block_size + _log2_num_sets)) % (ADDRESS_TAG_MASK + 1);
        uint16_t hashed_pc = CRC(PC) % log2_num_pred_entries;
        uint8_t timestamp = set_timestamp_counter[set_index];
        *curr_timestamp = timestamp;

        if (!sample_data[set_index].access(addr_tag, hashed_pc, timestamp, last_PC, last_timestamp)) {
            sample_data[set_index].insert(addr_tag, hashed_pc, timestamp);
            set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
            return false;
        }

        set_timestamp_counter[set_index] = (set_timestamp_counter[set_index] + 1) % _timer_size;
        return true;
    } else {
        return false;
    }
}


} // namespace gem5
