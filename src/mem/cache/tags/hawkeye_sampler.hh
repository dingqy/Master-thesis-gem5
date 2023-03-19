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

#include "base/sat_counter.hh"

namespace gem5 {

#define NUM_WAY_CACHE_SET 8UL
#define HASHED_PC_LEN 16UL
#define HASHED_PC_MASK ((1UL << HASHED_PC_LEN) - 1)
#define TIMESTAMP_LEN 8UL
#define TIMESTAMP_LEN_MASK ((1UL << TIMESTAMP_LEN) - 1)
#define ADDRESS_TAG_LEN 16UL
#define ADDRESS_TAG_MASK ((1UL << ADDRESS_TAG_LEN) - 1)


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

      uint64_t entry_tag;

      uint16_t getAddress() {
        return (entry_tag >> (TIMESTAMP_LEN + HASHED_PC_LEN)) & ADDRESS_TAG_MASK;
      }

      uint16_t getPC() {
        return (entry_tag >> (TIMESTAMP_LEN)) & HASHED_PC_MASK;
      }

      uint8_t getTimestamp() {
        return entry_tag & TIMESTAMP_LEN_MASK;
      }

      void setPC(uint16_t PC) {
        uint8_t timestamp = entry_tag & TIMESTAMP_LEN_MASK;
        entry_tag = (entry_tag >> (TIMESTAMP_LEN + HASHED_PC_LEN)) << (TIMESTAMP_LEN + HASHED_PC_LEN) ;
        entry_tag |= ((uint64_t) PC) << 8;
        entry_tag |= timestamp;
      }

      void setTimestamp(uint8_t timestamp) {
        entry_tag = ((entry_tag >> TIMESTAMP_LEN) << TIMESTAMP_LEN) | timestamp;
      }

      CacheLine() : valid(false), lru(0), entry_tag(0) {};

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
            ways[i].entry_tag = (((uint64_t) addr_tag) << (TIMESTAMP_LEN + HASHED_PC_LEN)) | (((uint64_t) PC) << (TIMESTAMP_LEN)) | ((uint64_t) timestamp);
            ways[i].valid = true;

            debug_insert = true;
            break;
          }
        }

        assert(debug_insert);
      }

      bool access(uint16_t addr_tag, uint16_t PC, uint8_t timestamp, uint16_t *last_PC, uint8_t *last_timestamp) {

        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (addr_tag == ways[i].getAddress()) {
            *last_PC = ways[i].getPC();
            *last_timestamp = ways[i].getTimestamp();

            ways[i].setPC(PC);
            ways[i].setTimestamp(timestamp);
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

    int _num_sets;

    int _num_cache_sets;

    int _cache_block_size;

    int _log2_num_sets;

    int _log2_cache_block_size;

    int _timer_size;

  public:

    HistorySampler(const int num_sets, const int num_cache_sets, const int cache_block_size, const int timer_size);

    ~HistorySampler();

    bool sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, int log2_num_pred_entries);

    uint64_t getCurrentTimestamp(int set);

};

class OccupencyVector
{

  private:
    std::vector<unsigned int> liveness_history;

    uint64_t num_cache;

    uint64_t num_dont_cache;

    uint64_t access;

    uint64_t CACHE_SIZE;

    uint64_t vector_size;

  public:
    OccupencyVector(const uint64_t cache_size, const uint64_t capacity);

    ~OccupencyVector() = default;

    void add_access(uint64_t curr_quanta);

    void add_prefetch(uint64_t curr_quanta);

    bool should_cache(uint64_t curr_quanta, uint64_t last_quanta);

    uint64_t get_num_opt_hits();

    uint64_t get_vector_size();
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
