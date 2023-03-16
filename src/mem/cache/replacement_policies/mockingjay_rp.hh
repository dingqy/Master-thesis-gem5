/**
 * Hawkeye cache replacement policy
 * 
 * Reference link: https://www.cs.utexas.edu/~lin/papers/isca16.pdf
 */

#ifndef __MEM_CACHE_REPLACEMENT_POLICIES_MOCKINGJAY_RP_HH__
#define __MEM_CACHE_REPLACEMENT_POLICIES_MOCKINGJAY_RP_HH__

#include "base/types.hh"
#include "mem/cache/replacement_policies/base.hh"
#include "base/sat_counter.hh"

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
        SatCounter8 rrpv;

        /** Cacheline type */
        bool is_cache_friendly;

        /** Whether the entry is valid. */
        bool valid;
        
        /**
         * Default constructor. Invalidate data.
         */
        MockingjayReplData(const int num_bits) : rrpv(num_bits), valid(false), is_cache_friendly(false) {}
    };

  public:
    typedef MockingjayRPParams Params;
    Mockingjay(const Params &p);
    ~Mockingjay() = default;

    /** History Sampler */

    /** Occupancy Vector */

    /** PC-based Binary Classifier */

    const unsigned int numRRPVBits;


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

    /**
     * Reset replacement data. Used when an entry is inserted.
     * 
     * This is where cache miss handling actually happens. Immediate cache miss will be stored in the MSHR until low-level memory components return that cache line
     *
     * @param replacement_data Replacement data to be reset.
     */
    void reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) override;

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