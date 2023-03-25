#include "mem/cache/replacement_policies/mockingjay_rp.hh"
#include "base/logging.hh" // For fatal_if
#include "params/MockingjayRP.hh"

namespace gem5 {

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

/**
 *  Parameters:
 *    1. num_etr_bits (ETR bits)
 *    2. num_cache_sets (Number of target cache sets)
 *    3. cache_block_size (Number of target cache block size)
 *    4. num_cache_ways (Number of target cache ways)
 *    5. num_cpu (Number of cores)
 *    6. num_pred_entries (Number of predictor entries)
 *    7. pred_num_bits_per_entry (Number of counter bits per entry in predictor)
 *    8. num_sampled_sets (Number of sets in sampled cache)
 *    9. timer_size (Number of bits for timestamp)
 * 
 */
Mockingjay::Mockingjay(const Params &p) : Base(p), _num_etr_bits(p.num_etr_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_sets((int) std::log2(p.num_cache_sets)) {
    sampled_cache = new SampledCache(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size, p.num_cpu);
    predictor = new ReuseDistPredictor(p.num_pred_entries, p.pred_num_bits_per_entry);
    age_ctr = new uint8_t[p.num_cache_sets];
}

Mockingjay::~Mockingjay() {
    delete sampled_cache;
    delete predictor;
    delete[] age_ctr;
}

void Mockingjay::invalidate(const std::shared_ptr<ReplacementData> &replacement_data)
{
    std::shared_ptr<MockingjayReplData> casted_replacement_data =
        std::static_pointer_cast<MockingjayReplData>(replacement_data);

    // Invalidate entry
    casted_replacement_data->valid = false;
    casted_replacement_data->etr = 0;
}

ReplaceableEntry* Mockingjay::getVictim(const ReplacementCandidates& candidates) const {
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use first candidate as dummy victim
    ReplaceableEntry* victim = candidates[0];

    // Store victim->etr in a variable to improve code readability
    int victim_etr = std::static_pointer_cast<MockingjayReplData>(
                        victim->replacementData)->etr;

    // Visit all candidates to find victim
    for (const auto& candidate : candidates) {
        std::shared_ptr<MockingjayReplData> candidate_repl_data =
            std::static_pointer_cast<MockingjayReplData>(
                candidate->replacementData);

        // Stop searching for victims if an invalid entry is found
        if (!candidate_repl_data->valid) {
            return candidate;
        }

        // Update victim entry if necessary
        int candidate_etr = candidate_repl_data->etr;
        if (candidate_etr > victim_etr) {
            victim = candidate;
            victim_etr = candidate_etr;
        }
    }

    // TODO: It can bypass the cache

    return victim;
}

}

}