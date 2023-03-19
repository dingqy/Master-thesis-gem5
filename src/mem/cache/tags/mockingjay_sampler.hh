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

namespace gem5 {

constexpr int ENTRY_ADDR = 10;
constexpr int ENTRY_ADDR_MASK = 0x3FF;
constexpr int ENTRY_PC = 11;
constexpr int ENTRY_PC_MASK = 0x7FF;
constexpr int ENTRY_TIMESTAMP = 8;
constexpr int ENTRY_TIMESTAMP_MASK = 0xFF;
constexpr int NUM_WAYS_SAMPLER = 5;

class SampledCachePacket
{

  public:
    uint16_t addr_tag;

    uint16_t hashed_PC;

    uint8_t timestamp;

    uint32_t target_set_index;

    SampledCachePacket(uint64_t addr, uint64_t PC, uint8_t timestamp) {}

    uint64_t entry_gen() {
      return ((addr_tag & ENTRY_ADDR_MASK) << (ENTRY_PC + ENTRY_TIMESTAMP)) | ((hashed_PC & ENTRY_PC_MASK) << ENTRY_TIMESTAMP) | (timestamp & ENTRY_TIMESTAMP_MASK);
    }
};

class SampledCache
{

  protected:
    struct CacheLine
    {

      bool valid;

      uint8_t lru;

      uint64_t entry_tag;

      uint16_t getAddress() {
        return (entry_tag >> (ENTRY_PC + ENTRY_TIMESTAMP)) & ENTRY_ADDR_MASK;
      }

      uint16_t getPC() {
        return (entry_tag >> ENTRY_TIMESTAMP) & ENTRY_PC_MASK;
      }

      uint8_t getTimestamp() {
        return entry_tag & ENTRY_TIMESTAMP_MASK;
      }

      void setPC(uint16_t PC) {
        uint64_t temp_high = entry_tag >> (ENTRY_TIMESTAMP + ENTRY_PC);
        uint64_t temp_low = entry_tag & ENTRY_PC_MASK;
        entry_tag = (temp_high << (ENTRY_TIMESTAMP + ENTRY_PC)) | ((PC & ENTRY_PC_MASK) << ENTRY_TIMESTAMP) | temp_low;
      }

      void setTimestamp(uint8_t timestamp) {
        entry_tag = ((entry_tag >> ENTRY_TIMESTAMP) << ENTRY_TIMESTAMP) | (timestamp & ENTRY_TIMESTAMP_MASK);
      }

      CacheLine() : valid(false), lru(0), entry_tag(0) {};

    };

    struct CacheSet
    {
      struct CacheLine ways[NUM_WAYS_SAMPLER];

      void insert(SampledCachePacket *pkt) {
        int victim = -1;

        for (int i = 0; i < NUM_WAYS_SAMPLER; i++) {
          if (!ways[i].valid) {
            victim = i;
          }
        }

        if (victim == -1) {
          for (int i = 0; i < NUM_WAYS_SAMPLER; i++) {
            if (ways[i].lru == 0) {
              victim = i;
            }
          }
        }

        assert(victim != -1);
        for (int j = 0; j < NUM_WAYS_SAMPLER; j++) {
          if (ways[j].valid && ways[j].lru > 0) {
            ways[j].lru -= 1;
          }
        }
        ways[victim].entry_tag = pkt->entry_gen();
        ways[victim].lru = NUM_WAYS_SAMPLER - 1;
        ways[victim].valid = true;
      }

      bool access(SampledCachePacket *pkt, uint16_t *last_PC, uint8_t *last_timestamp) {
        for (int i = 0; i < 128; i++) {
          if (pkt->addr_tag == ways[i].getAddress()) {
            *last_PC = ways[i].getPC();
            *last_timestamp = ways[i].getTimestamp();
            ways[i].setPC(pkt->hashed_PC);
            ways[i].setTimestamp(pkt->timestamp);
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

    std::default_random_engine generator;

  public:

    SampledCache(const int max_num_sample_sets, const int num_sets);

    ~SampledCache();

    bool sample(uint64_t addr, uint64_t PC, uint8_t timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp);

};


class RDP
{

};

}
