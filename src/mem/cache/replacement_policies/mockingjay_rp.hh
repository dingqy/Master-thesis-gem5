/**
 * Hawkeye cache replacement policy
 *
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_MOCKINGJAY_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_MOCKINGJAY_RP_HH__

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "mem/cache/tags/mockingjay_sampler.hh"

namespace gem5
{

struct MockingjayRPParams;

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

class Mockingjay : public Base
{
  protected:
    struct MockingjayReplData : ReplacementData
    {
        /**
         * Re-Reference Interval Prediction Value.
         * 0 -> cache friently (hit, miss)
         * max_RRPV-1 -> cache-averse (hit, miss)
         * RRPV value will be aged when cache miss occurs on a cache friendly line
         *
         * Allow multiple max_RRPV-1 to exist and will choose based on the index of the cache line
         */
        int8_t etr;

        const int abs_max_etr;

        /** Whether the entry is valid. */
        bool valid;

        void aging() {
          if (std::abs(etr) < abs_max_etr) {
            etr -= 1;
          }
        }

        /**
         * Default constructor. Invalidate data.
         */
        MockingjayReplData(const int num_bits) : etr(0), valid(false), abs_max_etr((1 << (num_bits - 1)) - 1) {}
    };

  public:
    typedef MockingjayRPParams Params;
    Mockingjay(const Params &p);
    ~Mockingjay() = default;

    /** History Sampler */
    SampledCache *sampled_cache;

    /** Reuse Distance Predictor */
    ReuseDistPredictor *predictor;

    /** Number of bits of ETR counter */
    const uint32_t _num_etr_bits;

    /** Clock age counter for each set */
    uint8_t *age_ctr;

    /** Number of bits of ETR counters */
    const int _num_etr_bits;

    /** Number of bits of cache block size */
    const int _log2_block_size;

    /** Number of bits of sampled cache sets */
    const int _log2_num_sets;

    /** Numer of bits of aging clock */
    const int _num_clock_bits;

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
    void touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt, const ReplacementCandidates& candidates) override;
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

#endif // __MEM_CACHE_REPLACEMENT_POLICIES_MOCKINGJAY_RP_HH__
