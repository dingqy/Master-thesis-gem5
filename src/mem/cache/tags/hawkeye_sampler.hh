/**
 * Hawkeye cache replacement policy
 * 
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#include "base/sat_counter.hh"
#include <vector>
#include <set>
#include <random>

namespace gem5 {

/**
 * Entry: 2-byte Address, 2-byte PC, 1-byte timestamp
 * 
 * Sample the whold cache with 64 random sets. The prediction need 8x history of set associativity
 * 
 * Set-associative cache: Each set has 128 entries (??? Which structure of this sampled cache)
 * 
 * LRU replacement policy
 */
class HistorySampler {

  protected:
    struct CacheLine {
      
      bool valid;

      uint8_t lru;

      uint64_t entry_tag;

      uint16_t getAddress() {
        return (entry_tag >> 24) & 0xFFFF;
      }

      uint16_t getPC() {
        return (entry_tag >> 8) & 0xFFFF;
      }

      uint8_t getTimestamp() {
        return entry_tag & 0xFF;
      }

      void setPC(uint16_t PC) {
        entry_tag = entry_tag & 0xFFFF0000FF;
        entry_tag |= ((uint64_t) PC) << 8;
      }

      void setTimestamp(uint8_t timestamp) {
        entry_tag = (entry_tag & 0xFFFFFFFF00) | timestamp;
      }

      CacheLine() : valid(false), lru(0), entry_tag(0) {};

    };

    struct CacheSet {
      struct CacheLine ways[128];
      
      void insert(uint64_t addr, uint64_t PC, uint8_t timestamp) {
        bool debug_insert = false;

        for (int i = 0; i < 128; i++) {
          if (!ways[i].valid || (ways[i].valid && ways[i].lru == 0)) {
            for (int j = 0; j < 128; j++) {
              if (ways[j].valid && ways[j].lru > 0) {
                ways[j].lru -= 1;
              }
            }
            // TODO: Insert into the cache line

            debug_insert = true;
            break;
          }
        }

        assert(debug_insert);
      }

      bool access(uint64_t addr, uint64_t PC, uint8_t timestamp, uint16_t *last_PC, uint8_t *last_timestamp) {
        // TODO: address tag
        uint16_t addr_tag;

        for (int i = 0; i < 128; i++) {
          if (addr_tag == ways[i].getAddress()) {
            *last_PC = ways[i].getPC();
            *last_timestamp = ways[i].getTimestamp();

            // TODO: PC
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
    std::set<uint32_t> target_sample_sets;

    struct CacheSet *sample_data;

    int _num_sets;

    int _num_ways;

    std::default_random_engine generator;

  public:

    HistorySampler(const int max_num_sample_sets, const int num_sets);

    ~HistorySampler();

    bool sample(uint64_t addr, uint64_t PC, uint8_t timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp);

};

/**
 * 4-bit per entry, 32 entries per vector, 1 vector per set, 64 sets
 * 
 * TODO: Is this be updated when cache miss immediatly occurs or until it is inserted into the cache? 
 */
class OccupencyVector {

  private:

    int size = 0;

    int max_num_element = 0;

    int max_value_entry = 0;
  
    std::vector<uint8_t> data;

    int head = 0;

    int tail = 0;

  public:

    OccupencyVector(const int vector_size, const int bits_per_entry);
    
    ~OccupencyVector() = default;

    bool opt_result_gen(const uint8_t last_timestamp, int cache_capacity);

    uint8_t push(uint8_t value);
};

/**
 * 3-bit saturating counter, high-order bit determines whether it is cache-averse (0) or cache-friendly (1)
 * 
 * 8K entries (2^13 => 13-bit hashed PC index)
 */
class PCBasedPredictor {

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

};

}