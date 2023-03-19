#include "mem/cache/replacement_policies/hawkeye_rp.hh"

#include "base/logging.hh"
#include "params/HawkeyeRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


Hawkeye::Hawkeye(const Params &p) : Base(p), _num_rrpv_bits(p.num_rrpv_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_sets((int) std::log2(p.num_cache_sets)) {
    // Paramters:
    //  1. num_rrpv_bits (RRPV bits)
    //  2. num_cache_sets (Number of target cache sets)
    //  3. cache_block_size (Number of target cache block size)
    //  4. num_cache_ways (Number of target cache ways)
    //  5. optgen_vector_size (The size of occupancy vector)
    //  6. num_pred_entries (Number of predictor entries)
    //  7. pred_num_bits_per_entry (Number of counter bits per entry in predictor)
    //  8. num_sampled_sets (Number of sets in sampled cache)
    //  9. timer_size (Number of bits for timer)

    //const int num_sets, const int num_cache_sets, const int cache_block_size
    sampler = new HistorySampler(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size);
    predictor = new PCBasedPredictor(p.num_pred_entries, p.pred_num_bits_per_entry);
    opt_vector = new OccupencyVector(p.num_cache_ways, p.optgen_vector_size);
}

Hawkeye::~Hawkeye() {
    delete sampler;
    delete predictor;
    delete opt_vector;
}

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

    if (!pkt->req->hasPC()) {
        return;
    }

    if (casted_replacement_data->is_cache_friendly) {
        casted_replacement_data->rrpv.reset();
    } else {
        casted_replacement_data->rrpv.saturate();
    }

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_sets) - 1);

    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;
    // TODO: If the cacheline becomes to sample sets, record the history
    // uint64_t addr, uint64_t PC, uint8_t timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, int log2_num_pred_entries
    if (sampler->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp, predictor->log2_num_entries())) {
        curr_timestamp = curr_timestamp % opt_vector->get_vector_size();

        // sample hit
        predictor->train(last_PC, opt_vector->should_cache(curr_timestamp, last_timestamp));
    }
}

std::shared_ptr<ReplacementData> Hawkeye::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new HawkeyeReplData(_num_rrpv_bits));
}

void Hawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    if (!pkt->req->hasPC()) {
        return;
    }

    bool is_friendly = predictor->predict(pkt->req->getPC());

    casted_replacement_data->is_cache_friendly = is_friendly;

    if (is_friendly) {
        casted_replacement_data->rrpv.saturate();
    } else {
        casted_replacement_data->rrpv.reset();
    }

    casted_replacement_data->valid = true;

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_sets) - 1);

    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;
    // TODO: If the cacheline becomes to sample sets, record the history
    // uint64_t addr, uint64_t PC, uint8_t timestamp, int set, uint16_t *last_PC, uint8_t *last_timestamp, int log2_num_pred_entries
    if (sampler->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp, predictor->log2_num_entries())) {
        curr_timestamp = curr_timestamp % opt_vector->get_vector_size();

        // sample hit
        predictor->train(last_PC, opt_vector->should_cache(curr_timestamp, last_timestamp));
    }
}

void Hawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

void Hawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

} // namespace replacement_policy
} // namespace gem5
