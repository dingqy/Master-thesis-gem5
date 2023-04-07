/**
 * Hawkeye cache replacement policy
 *
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_HAWKEYE_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_HAWKEYE_RP_HH__

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/tags/hawkeye_sampler.hh"
#include "base/trace.hh"
#include "debug/HawkeyeReplDebug.hh"

namespace gem5
{

struct HawkeyeRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class Hawkeye : public Base
{
  protected:
    struct HawkeyeReplData : ReplacementData
    {
        /**
         * Re-Reference Interval Prediction Value.
         * 0 -> cache friently (hit, miss)
         * max_RRPV-1 -> cache-averse (hit, miss)
         * RRPV value will be aged when cache miss occurs on a cache friendly line
         *
         * Allow multiple max_RRPV-1 to exist and will choose based on the index of the cache line
         */
        int rrpv;

        int max_rrpv;

        /** Cacheline type */
        bool is_cache_friendly;

        /** Whether the entry is valid. */
        bool valid;

        int context_id;

        void saturate() {
          rrpv = max_rrpv;
        }

        void reset() {
          rrpv = 0;
        }

        /**
         * Default constructor. Invalidate data.
         */
        HawkeyeReplData(const int num_bits) : rrpv(0), max_rrpv((1 << num_bits) - 1), is_cache_friendly(false), valid(false), context_id(0) {}
    };

    struct RatioCounter {
      int counter;

      int ratio_max;

      RatioCounter() : counter(0), ratio_max(0) {}
    };

  public:
    typedef HawkeyeRPParams Params;
    Hawkeye(const Params &p);
    ~Hawkeye() = default;

    /** History Sampler */
    std::unique_ptr<HistorySampler> sampler;

    /** Occupancy Vector */
    std::unique_ptr<OccupencyVector> opt_vector;

    /** PC-based Binary Classifier */
    std::unique_ptr<PCBasedPredictor> predictor;

    /** Number of RRPV bits */
    const int _num_rrpv_bits;

    /** Number of bits of target cache block size */
    const int _log2_block_size;

    /** Number of bits of target cache set */
    const int _log2_num_cache_sets;

    const int _num_cpus;

    const int _num_cache_ways;

    const int _cache_level;

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
