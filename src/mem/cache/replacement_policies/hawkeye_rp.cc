#include "mem/cache/replacement_policies/hawkeye_rp.hh"

#include "base/logging.hh"
#include "params/HawkeyeRP.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


Hawkeye::Hawkeye(const Params &p) : Base(p), _num_rrpv_bits(p.num_rrpv_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_cache_sets((int) std::log2(p.num_cache_sets)),
                                    _num_cpus(p.num_cpus), _num_cache_ways(p.num_cache_ways), _cache_level(p.cache_level) {
    // Paramters:
    //  1. num_rrpv_bits (RRPV bits)
    //  2. num_cache_sets (Number of target cache sets)
    //  3. cache_block_size (Number of target cache block size)
    //  4. num_cache_ways (Number of target cache ways)
    //  5. optgen_vector_size (The size of occupancy vector)
    //  6. num_pred_entries (Number of predictor entries)
    //  7. num_pred_bits (Number of counter bits per entry in predictor)
    //  8. num_sampled_sets (Number of sets in sampled cache)
    //  9. timer_size (The size of timer for recording current timestamp)

    sampler = std::make_unique<HistorySampler>(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size);
    predictor = std::make_unique<PCBasedPredictor>(p.num_pred_entries, p.num_pred_bits);
    for (int i = 0; i < p.num_cache_sets; i++) {
        opt_vector.push_back(std::make_unique<OccupencyVector>(p.num_cache_ways, p.optgen_vector_size));
    }

    DPRINTF(HawkeyeReplDebug, "Cache Initialization ---- Number of Cache Sets: %d, Cache Block Size: %d, Number of Cache Ways: %d\n", p.num_cache_sets, p.cache_block_size, p.num_cache_ways);
    DPRINTF(HawkeyeReplDebug, "History Sampler Initialization ---- Number of Sample Sets: %d, Timer Size: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(HawkeyeReplDebug, "Occupancy Vector Initialization ---- Vector size: %d\n", p.optgen_vector_size);
    DPRINTF(HawkeyeReplDebug, "Predictor Initialization ---- Number of Predictor Entries: %d, Counter of Predictors: %d\n", p.num_pred_entries, p.num_pred_bits);
}

void Hawkeye::invalidate(const std::shared_ptr<ReplacementData> &replacement_data)
{
    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    // Invalidate entry

    // TODO: If it is sampled cache line, then that cache line should be invalidated also.
    casted_replacement_data->valid = false;
    casted_replacement_data->is_cache_friendly = false;
}

void Hawkeye::access(const PacketPtr pkt, bool hit, const ReplacementCandidates& candidates) {}

ReplaceableEntry* Hawkeye::getVictim(const ReplacementCandidates& candidates) const {
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use first candidate as dummy victim
    ReplaceableEntry* victim = candidates[0];

    // Store victim->rrpv in a variable to improve code readability
    int victim_RRPV = std::static_pointer_cast<HawkeyeReplData>(
                        victim->replacementData)->rrpv;

    // Visit all candidates to find victim
    // If there is no invalid cache line, the one with highest RRPV will be evicted
    // TODO: Bypass cache should be possible (return nullptr)
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
        // TODO: Is saturated counter support comparison?
        // TODO: Hard code 3-bit RRPV here
        if (temp->valid && temp->is_cache_friendly && temp->rrpv < 6) {
            temp->rrpv++;
        }
        panic_if(temp->rrpv > 6 && temp->is_cache_friendly, "Friendly cache should never be the maximum value of RRPV (6)");
    }

    return victim;
}

void Hawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {
    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    // TODO: Which requests should we monitor?
    if (!pkt->isRequest() || !pkt->req->hasPC() || !pkt->req->hasContextId()) {
        DPRINTF(HawkeyeReplDebug, "Cache hit (Packet not valid for further action) ---- Request: %d, PC %d, Context ID: %d\n", pkt->isRequest(), pkt->req->hasPC(), pkt->req->hasContextId());
        DPRINTF(HawkeyeReplDebug, "Cache hit ---- Packet type: %s\n", pkt->cmdString());
        return;
    }

    DPRINTF(HawkeyeReplDebug, "Cache hit ---- Packet type having PC: %s\n", pkt->cmdString());

    if (casted_replacement_data->is_cache_friendly) {
        casted_replacement_data->reset();
    } else {
        casted_replacement_data->saturate();
    }
    casted_replacement_data->context_id = pkt->req->contextId();

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_cache_sets) - 1);

    DPRINTF(HawkeyeReplDebug, "Cache hit ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    // Warning: Timestamp is 8-bit integer in this design
    uint8_t curr_timestamp = 0;
    uint8_t last_timestamp = 0;
    uint16_t last_PC = 0;

    if (sampler->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp)) {
        
        curr_timestamp = curr_timestamp % opt_vector[set]->get_vector_size();

        DPRINTF(HawkeyeReplDebug, "Cache hit ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictor->train(last_PC, opt_vector[set]->should_cache(curr_timestamp, last_timestamp));
        opt_vector[set]->add_access(curr_timestamp);
    }
}

std::shared_ptr<ReplacementData> Hawkeye::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new HawkeyeReplData(_num_rrpv_bits));
}

void Hawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    if (!pkt->isResponse() || !pkt->req->hasPC() || !pkt->req->hasContextId()) {
        DPRINTF(HawkeyeReplDebug, "Cache miss (Packet not valid for further action) ---- Request: %d, PC %d, Context ID: %d\n", pkt->isRequest(), pkt->req->hasPC(), pkt->req->hasContextId());
        DPRINTF(HawkeyeReplDebug, "Cache miss handling ---- Packet type: %s\n", pkt->cmdString());
        return;
    }

    DPRINTF(HawkeyeReplDebug, "Cache miss handling ---- Packet type having PC: %s\n", pkt->cmdString());

    bool is_friendly = predictor->predict(pkt->req->getPC());

    casted_replacement_data->is_cache_friendly = is_friendly;

    if (is_friendly) {
        casted_replacement_data->reset();
    } else {
        casted_replacement_data->saturate();        
    }

    casted_replacement_data->valid = true;
    casted_replacement_data->context_id = pkt->req->contextId();

    DPRINTF(HawkeyeReplDebug, "Cache miss handling ---- New Cache Line: Friendliness %d RRPV: %d Valid: %d\n", casted_replacement_data->is_cache_friendly, 
            casted_replacement_data->rrpv, casted_replacement_data->valid);

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_cache_sets) - 1);

    DPRINTF(HawkeyeReplDebug, "Cache miss handling ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    uint8_t curr_timestamp = 0;
    uint8_t last_timestamp = 0;
    uint16_t last_PC = 0;

    if (sampler->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp)) {
        curr_timestamp = curr_timestamp % opt_vector[set]->get_vector_size();
        DPRINTF(HawkeyeReplDebug, "Cache miss handling ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictor->train(last_PC, opt_vector[set]->should_cache(curr_timestamp, last_timestamp));
        opt_vector[set]->add_access(curr_timestamp);
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
