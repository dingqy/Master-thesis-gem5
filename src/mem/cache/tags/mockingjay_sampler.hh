/**
 * Mockingjay cache replacement policy
 *
 * Reference link: https://www.cs.utexas.edu/~lin/papers/hpca22.pdf
 *
 */
#include <cassert>
#include <cstdint>
#include <random>
#include <set>
#include "debug/CacheRepl.hh"
#include "base/trace.hh"
#include "base/sat_counter.hh"

namespace gem5 {

constexpr uint32_t NUM_WAY_CACHE_SET = 5;
constexpr uint32_t HASHED_PC_LEN = 11;
constexpr uint64_t HASHED_PC_MASK = ((1 << HASHED_PC_LEN) - 1);
constexpr uint32_t TIMESTAMP_LEN = 8;
constexpr uint64_t TIMESTAMP_LEN_MASK = ((1 << TIMESTAMP_LEN) - 1);
constexpr uint32_t ADDRESS_TAG_LEN = 10;
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
class SampledCache
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

      bool insert(uint16_t addr_tag, uint16_t PC, uint8_t timestamp, uint8_t *evict_timestamp, uint16_t *evict_signature) {
        // Assume address and PC has been translated
        bool insert_with_evict = false;
        int evict_way = -1;

        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (!ways[i].valid) {
            evict_way = i;
          }
        }

        int evict_lru = NUM_WAY_CACHE_SET;
        if (evict_way < 0) {
          for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
            if (ways[i].valid && ways[i].lru < evict_lru) {
              evict_lru = ways[i].lru;
              evict_way = i;
            } else if (ways[i].lru == evict_lru) {
              panic("LRU for sampled cache should not have the same value");
            }
          }
          gem5_assert(evict_way >= 0, "There should be one cache line evicted.");
          insert_with_evict = true;
        }

        *evict_signature = ways[evict_way].getPC();
        *evict_timestamp = ways[evict_way].getTimestamp();
        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (ways[i].valid && ways[i].lru > evict_lru && ways[i].lru > 0) {
            ways[i].lru -= 1;
          }
        }
        ways[evict_way].lru = NUM_WAY_CACHE_SET - 1;
        ways[evict_way].setAddrTag(addr_tag);
        ways[evict_way].setPC(PC);
        ways[evict_way].setTimestamp(timestamp);
        ways[evict_way].valid = true;

        return insert_with_evict;
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

      bool invalidate(uint16_t addr_tag) {
        for (int i = 0; i < NUM_WAY_CACHE_SET; i++) {
          if (addr_tag == ways[i].getAddress()) {
            ways[i].valid = false;
            for (int j = 0; j < NUM_WAY_CACHE_SET; j++) {
              if (i != j && ways[j].lru > ways[i].lru) {
                ways[j].lru -= 1;
              }
            }
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

    int _num_cpus;

  public:

    SampledCache(const int num_sets, const int num_cache_sets, const int cache_block_size, const int timer_size, const int num_cpu);

    ~SampledCache();

    bool sample(uint64_t addr, uint64_t PC, uint8_t *curr_timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, bool hit, bool *evict, bool *sample_hit, int core_id);

    uint64_t getCurrentTimestamp(int set);

};

/**
 * 3-bit saturating counter, high-order bit determines whether it is cache-averse (0) or cache-friendly (1)
 *
 * 8K entries (2^11 => 13-bit hashed PC index)
 */
class ReuseDistPredictor
{

  private:

    int *counters;

    int num_entries;

    int max_value = 0;

    int bits_per_entry = 0;

    int max_rd = 0;

    int _granularity;

    int _num_cpus;

  public:

    ReuseDistPredictor(const int num_entries, const int bits_per_entry, const int aging_clock_size, const int num_cpus);

    ~ReuseDistPredictor();

    void train(uint64_t last_PC, bool sampled_cache_hit, uint8_t curr_timestamp, uint8_t last_timestamp);

    uint16_t predict(uint64_t PC, bool hit, int core_id, int etr_inf);

    int log2_num_entries();
};

}
