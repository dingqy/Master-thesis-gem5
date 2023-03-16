#include "mem/cache/replacement_policies/hawkeye_rp.hh"
#include "base/logging.hh" // For fatal_if
#include "params/HawkeyeRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


Hawkeye::Hawkeye(const Params &p) : Base(p), numRRPVBits(p.num_bits) {}

void Hawkeye::invalidate(const std::shared_ptr<ReplacementData> &replacement_data)
{
    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    // Invalidate entry
    casted_replacement_data->valid = false;
    casted_replacement_data->is_cache_friendly = false;
}

ReplaceableEntry* Hawkeye::getVictim(const ReplacementCandidates& candidates) const {
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use first candidate as dummy victim
    ReplaceableEntry* victim = candidates[0];

    // Store victim->rrpv in a variable to improve code readability
    int victim_RRPV = std::static_pointer_cast<HawkeyeReplData>(
                        victim->replacementData)->rrpv;

    // Visit all candidates to find victim
    for (const auto& candidate : candidates) {
        std::shared_ptr<HawkeyeReplData> candidate_repl_data =
            std::static_pointer_cast<HawkeyeReplData>(
                candidate->replacementData);

        // Stop searching for victims if an invalid entry is found
        if (!candidate_repl_data->valid) {
            return candidate;
        }

        // Update victim entry if necessary
        int candidate_RRPV = candidate_repl_data->rrpv;
        if (candidate_RRPV > victim_RRPV) {
            victim = candidate;
            victim_RRPV = candidate_RRPV;
        }
    }

    // Update RRPV of all candidates
    for (const auto& candidate : candidates) {
        std::shared_ptr<HawkeyeReplData> temp = 
            std::static_pointer_cast<HawkeyeReplData>(candidate->replacementData);
        if (temp->valid && temp->is_cache_friendly && temp->rrpv < 6) {
            temp->rrpv++;
        } 
    }

    return victim;
}

void Hawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {
    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    if (casted_replacement_data->is_cache_friendly) {
        casted_replacement_data->rrpv.reset();
    } else {
        casted_replacement_data->rrpv.saturate();
    }

    // TODO: If the cacheline becomes to sample sets, record the history
}

std::shared_ptr<ReplacementData> Hawkeye::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new HawkeyeReplData(numRRPVBits));
}

void reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    // TODO: Use PC-based binary classfier to determine whether the cacheline is cache-averse or cache-friendly

    // TODO: Setup the RRPV of target cacheline

    // TODO: If the cacheline becomes to sample sets, record the history

    // TODO: Train the predictor

}

} // namespace replacement_policy
} // namespace gem5