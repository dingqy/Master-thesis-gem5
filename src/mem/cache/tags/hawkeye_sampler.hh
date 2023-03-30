/**
 * Hawkeye cache replacement policy
 *
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#include <cmath>
#include <iostream>
#include <random>
#include <set>
#include <vector>
#include <unordered_map>
#include "base/sat_counter.hh"
#include "debug/CacheRepl.hh"
#include "base/trace.hh"

namespace gem5 {

// Warning: Sampled cache way is fixed (8)
constexpr uint32_t NUM_WAY_CACHE_SET = 8;
constexpr uint32_t HASHED_PC_LEN = 16;
constexpr uint64_t HASHED_PC_MASK = ((1 << HASHED_PC_LEN) - 1);
constexpr uint32_t TIMESTAMP_LEN = 8;
constexpr uint64_t TIMESTAMP_LEN_MASK = ((1 << TIMESTAMP_LEN) - 1);
constexpr uint32_t ADDRESS_TAG_LEN = 16;
constexpr uint64_t ADDRESS_TAG_MASK = ((1 << ADDRESS_TAG_LEN) - 1);


/**
 * Entry: 2-byte Address, 2-byte PC, 1-byte timestamp
 *
 * Sample the whold cache with 64 random sets. The prediction need 8x history of set associativity
 *
 * Set-associative cache: Each set has 128 entries (??? Which structure of this sampled cache)
 *
 * LRU replacement policy
 */
class HistorySampler
{

  protected:
    struct CacheLine
    {

      bool valid;

      uint8_t lru;

      uint64_t _address;

      uint64_t _pc;

      uint64_t _timestamp;

      uint16_t getAddress() {
        gem5_assert(_address < (1 << ADDRESS_TAG_LEN), "Address bits are wrong");
        return _address;
      }

      uint16_t getPC() {
        gem5_assert(_pc < (1 << HASHED_PC_LEN), "PC bits are wrong");
        return _pc;
      }

      uint8_t getTimestamp() {
        gem5_assert(_timestamp < (1 << TIMESTAMP_LEN), "timestamp bits are wrong");
        return _timestamp;
      }

      void setPC(uint16_t PC) {
        _pc = PC;
      }

      void setTimestamp(uint8_t timestamp) {
        _timestamp = timestamp;
      }

      void setAddrTag(uint16_t addrtag) {
        _address = addrtag;
      }

      CacheLine() : valid(false), lru(0), _address(0), _pc(0), _timestamp(0) {};

    };

    struct CacheSet
    {
      struct CacheLine ways[NUM_WAY_CACHE_SET];

      void insert(uint16_t addr_tag, uint16_t PC, uint8_t timestamp) {
        // Assume address and PC has been translated
        bool debug_insert = false;

        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (!ways[i].valid || (ways[i].valid && ways[i].lru == 0)) {
            for (int j = 0; j < NUM_WAY_CACHE_SET; j++) {
              if (ways[j].valid && ways[j].lru > 0) {
                ways[j].lru -= 1;
              }
            }
            ways[i].lru = NUM_WAY_CACHE_SET - 1;
            ways[i].setAddrTag(addr_tag);
            ways[i].setPC(PC);
            ways[i].setTimestamp(timestamp);
            ways[i].valid = true;

            debug_insert = true;
            break;
          }
        }

        gem5_assert(debug_insert, "Sampled cache insert fails");
      }

      bool access(uint16_t addr_tag, uint16_t PC, uint8_t timestamp, uint16_t *last_PC, uint8_t *last_timestamp) {

        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (addr_tag == ways[i].getAddress()) {
            *last_PC = ways[i].getPC();
            *last_timestamp = ways[i].getTimestamp();

            ways[i].setPC(PC);
            ways[i].setTimestamp(timestamp);

            for (int j = 0; j < NUM_WAY_CACHE_SET; j++) {
              if (ways[j].lru > ways[i].lru) {
                ways[j].lru -= 1;
              }
            }
            ways[i].lru = NUM_WAY_CACHE_SET - 1;
            return true;
          }
        }
        return false;
      }

      CacheSet() {};
    };

  private:
    struct CacheSet *sample_data;

    uint64_t *set_timestamp_counter;

    // Sampler sets
    int _num_sets;

    // Target cache sets
    int _num_cache_sets;

    int _cache_block_size;

    int _log2_num_sets;

    int _log2_cache_block_size;

    int _timer_size;

  public:

    HistorySampler(const int num_sets, const int num_cache_sets, const int cache_block_size, const int timer_size);

    ~HistorySampler();

    bool sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp);

    uint64_t getCurrentTimestamp(int set);

};

class OccupencyVector
{

  private:
    std::vector<unsigned int> liveness_history;

    std::unordered_map<int, uint64_t> num_cache;

    std::unordered_map<int, uint64_t> num_dont_cache;

    uint64_t access;

    uint64_t CACHE_SIZE;

    uint64_t vector_size;

  public:
    OccupencyVector(const uint64_t cache_size, const uint64_t capacity);

    ~OccupencyVector() = default;

    void add_access(uint64_t curr_quanta);

    void add_prefetch(uint64_t curr_quanta);

    bool should_cache(uint64_t curr_quanta, uint64_t last_quanta);

    uint64_t get_num_opt_hits(int cache_size);

    uint64_t get_num_opt_misses(int cache_size);

    uint64_t get_num_access() {
      return access;
    }

    uint64_t get_vector_size();

    void setCacheSize(uint64_t cache_size) {
      CACHE_SIZE = cache_size;
    }
};

/**
 * 3-bit saturating counter, high-order bit determines whether it is cache-averse (0) or cache-friendly (1)
 *
 * 8K entries (2^13 => 13-bit hashed PC index)
 */
class PCBasedPredictor
{

  private:

    int *counters;

    int num_entries;

    int max_value = 0;

    int bits_per_entry = 0;

  public:

    PCBasedPredictor(const int num_entries, const int bits_per_entry);

    ~PCBasedPredictor();

    void train(uint64_t last_PC, bool opt_decision);

    bool predict(uint64_t PC);

    int log2_num_entries();
};

}
