/**
 * Hawkeye cache replacement policy
 *
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_HAWKEYE_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_HAWKEYE_RP_HH__

#include <vector>
#include <map>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/tags/hawkeye_sampler.hh"

namespace gem5
{

static constexpr int REPARITITION_SIZE = 10000;
static constexpr int REAGING_SIZE = 10000;

class System;

struct FlockHawkeyeRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class FlockHawkeye : public Base
{
  protected:
    struct FlockHawkeyeReplData : ReplacementData
    {
        /**
         * Re-Reference Interval Prediction Value.
         * 0 -> cache friently (hit, miss)
         * max_RRPV-1 -> cache-averse (hit, miss)
         * RRPV value will be aged when cache miss occurs on a cache friendly line
         *
         * Allow multiple max_RRPV-1 to exist and will choose based on the index of the cache line
         */
        SatCounter8 rrpv;

        /** Cacheline type */
        bool is_cache_friendly;

        /** Whether the entry is valid. */
        bool valid;

        int context_id;

        /**
         * Default constructor. Invalidate data.
         */
        FlockHawkeyeReplData(const int num_bits) : rrpv(num_bits), is_cache_friendly(false), valid(false), context_id(0) {}
    };

    struct RatioCounter {
      int counter;

      int ratio_max;

      RatioCounter() : counter(0), ratio_max(0) {}
    };

  public:
    typedef FlockHawkeyeRPParams Params;
    FlockHawkeye(const Params &p);
    ~FlockHawkeye() = default;

    /** History Sampler */
    std::vector<std::unique_ptr<HistorySampler>> samplers;

    /** Occupancy Vector */
    std::vector<std::unique_ptr<OccupencyVector>> opt_vectors;

    /** PC-based Binary Classifier */
    std::vector<std::unique_ptr<PCBasedPredictor>> predictors;

    /** Projection vectors */
    std::vector<std::unique_ptr<OccupencyVector>> proj_vectors;

    /** Number of RRPV bits */
    const int _num_rrpv_bits;

    /** Number of bits of target cache block size */
    const int _log2_block_size;

    /** Number of bits of target cache set */
    const int _log2_num_cache_sets;

    const int _num_cpus;

    const int _num_cache_ways;

    const int _cache_level;

    // TODO: All per core infomation should be the same replacement policy class
    std::vector<RatioCounter> ratio_counter;

    // Partition budget
    std::vector<int> curr_partition; 

    /** Cache level + CPU id -> Cache miss count + Inst count*/
    std::map<std::pair<int, ContextID>, std::pair<Counter, Counter>> cache_stats;

    /** Cache level access latency + Number of cycles */
    std::map<std::pair<int, ContextID>, double> cache_latency_stats;

    std::map<ContextID, double> cpi_stats;

    Counter repartition;

    Counter reaging;

    Counter dram_stats[2]; // 0 - Access; 1 - Rowhits

    double dram_latency;

    bool dram_ready = false;

    double getCurrFCP(int core_id); 

    double getProjFCP(int core_id, int partition);

    void setNewPartition();

    void setAgingCounter();

    void access(const PacketPtr pkt, bool hit, const ReplacementCandidates& candidates) override;

    /**
     * Invalidate replacement data to set it as the next probable victim.
     *
     * Set RRPV value to be the maximum value (7) to relase the cache line for new cache line insertion.
     *
     * @param replacement_data Replacement data to be invalidated.
     */
    void invalidate(const std::shared_ptr<ReplacementData>& replacement_data) override;

    /**
     * Touch an entry to update its replacement data.
     *
     * This is where cache hit handling happens. It will not modify data but only update the RRPV values.
     *
     * @param replacement_data Replacement data to be touched.
     */
    void touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) override;
    void touch(const std::shared_ptr<ReplacementData>& replacement_data) const override;

    /**
     * Reset replacement data. Used when an entry is inserted.
     *
     * This is where cache miss handling actually happens. Immediate cache miss will be stored in the MSHR until low-level memory components return that cache line
     *
     * @param replacement_data Replacement data to be reset.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) override;
    void reset(const std::shared_ptr<ReplacementData>& replacement_data) const override;

    /**
     * Find replacement victim based on RRPV values.
     *
     * @param cands Replacement candidates, selected by indexing policy.
     * @return Replacement entry to be replaced.
     */
    ReplaceableEntry* getVictim(const ReplacementCandidates& candidates) const override;

    /**
     * Instantiate a replacement data entry.
     *
     * @return A shared pointer to the new replacement data.
     */
    std::shared_ptr<ReplacementData> instantiateEntry() override;

};

} // namespace replacement_policy
} // namespace gem5

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_HAWKEYE_RP_HH__
