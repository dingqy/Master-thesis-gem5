#include "mem/cache/tags/hawkeye_sampler.hh"
#include <cmath>
#include <cassert>

namespace gem5
{

OccupencyVector::OccupencyVector(const int vector_size, const int bits_per_entry): max_num_element(vector_size), head(0), tail(0), size(0) {
    data.resize(vector_size);
    max_value_entry = (int) std::pow(2, bits_per_entry) - 1;
}

uint8_t OccupencyVector::push(uint8_t value) {
    tail += 1;
    if (tail > max_num_element) {
        tail -= max_num_element;
    }
    if (tail == head) {
        head += 1;
        if (head > max_num_element) {
            head -= max_num_element;
        }
    }

    data[tail] = value;

    if (size < max_num_element) {
        size += 1;
    }

    return tail;
}

bool OccupencyVector::opt_result_gen(const uint8_t last_timestamp, const int cache_capacity) {
    uint8_t temp_index = last_timestamp;

    while (temp_index != tail) {
        if (data[temp_index] >= cache_capacity) {
            return false;
        }
        temp_index += 1;
        if (temp_index >= max_num_element) {
            temp_index -= max_num_element;
        }
    }

    temp_index = last_timestamp;
    while (temp_index != tail) {
        if (data[temp_index] < max_value_entry) {
            data[temp_index] += 1;
        }
    }

    return true;
}

PCBasedPredictor::PCBasedPredictor(const int num_entries, const int bits_per_entry): num_entries(num_entries), bits_per_entry(bits_per_entry) {
    counters = new int[num_entries];
    max_value = (int) (std::pow(2, bits_per_entry) - 1);
}

PCBasedPredictor::~PCBasedPredictor() {
    delete[] counters;
}

void PCBasedPredictor::train(uint64_t last_PC, bool opt_decision) {
    assert(last_PC < num_entries);
    if (opt_decision) {
        // Cache hit
        if (counters[last_PC] < max_value) {
            counters[last_PC] += 1;
        }
    } else {
        if (counters[last_PC] > 0) {
            counters[last_PC] -= 1;
        }
    }
}

bool PCBasedPredictor::predict(uint64_t PC) {
    assert(PC < num_entries);
    return (counters[PC] >> (bits_per_entry - 1)) & 0x1;
}

HistorySampler::HistorySampler(const int max_num_sample_sets, const int num_sets) : _num_sets(num_sets) {
    std::uniform_int_distribution<int> distribution(0, max_num_sample_sets - 1);
    for (int i = 0; i < 64; i++) {
        target_sample_sets.insert(distribution(generator));
    }
    sample_data = new CacheSet[num_sets];
}

HistorySampler::~HistorySampler() {
    delete[] sample_data;
}

bool HistorySampler::sample(uint64_t addr, uint64_t PC, uint8_t timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp) {
    if (target_sample_sets.find(set) != target_sample_sets.end()) {
        int set_index = 0;
        // TODO: Get set index from addr

        if (!sample_data[set_index].access(addr, PC, timestamp, last_PC, last_timestamp)) {
            sample_data[set_index].insert(addr, PC, timestamp);
            return false;
        }

        return true;
    }
}


} // namespace gem5
