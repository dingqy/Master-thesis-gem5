#include "mem/cache/replacement_policies/hawkeye_rp.hh"

#include "base/logging.hh"
#include "params/HawkeyeRP.hh"
#include "debug/CacheRepl.hh"
#include "base/trace.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


Hawkeye::Hawkeye(const Params &p) : Base(p), _num_rrpv_bits(p.num_rrpv_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_cache_sets((int) std::log2(p.num_cache_sets)),
                                    _num_cpus(p.num_cpus), _num_cache_ways(p.num_cache_ways), _cache_partition_on(p.cache_partition_on), _cache_level(p.cache_level) {
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
    
    int num_hawkeye;
    if (p.cache_partition_on) {
        num_hawkeye = p.num_cpus;
    } else {
        num_hawkeye = 1;
    }
    for (int i = 0; i < num_hawkeye; i++) {
        samplers.push_back(std::make_unique<HistorySampler>(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size));
        predictors.push_back(std::make_unique<PCBasedPredictor>(p.num_pred_entries, p.num_pred_bits));
        opt_vectors.push_back(std::make_unique<OccupencyVector>(p.num_cache_ways, p.optgen_vector_size));
        proj_vectors.push_back(std::make_unique<OccupencyVector>(p.num_cache_ways, p.optgen_vector_size));
    }

    curr_paritition.resize(p.num_cpus, 0);
    ratio_counter.resize(p.num_cpus);

    for (int i = 0; i < p.num_cpus; i++) {
        cache_stats[std::make_pair(p.cache_level, i)] = std::make_pair(0, 0);
    }

    DPRINTF(CacheRepl, "Cache Initialization ---- Number of Cache Sets: %d, Cache Block Size: %d, Number of Cache Ways: %d\n", p.num_cache_sets, p.cache_block_size, p.num_cache_ways);
    DPRINTF(CacheRepl, "History Sampler Initialization ---- Number of Sample Sets: %d, Timer Size: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(CacheRepl, "Occupancy Vector Initialization ---- Vector size: %d\n", p.optgen_vector_size);
    DPRINTF(CacheRepl, "Predictor Initialization ---- Number of Predictor Entries: %d, Counter of Predictors: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(CacheRepl, "Partition Initialization ---- Enforcement mechanism: %d\n", p.cache_partition_on);
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

void Hawkeye::access(const PacketPtr pkt, bool hit) {
    if ((pkt->isResponse() || pkt->isRequest()) && pkt->req->hasCacheStats()) {
        std::unordered_map<int, double>::iterator it = pkt->req->getCacheStatsBegin();
        std::unordered_map<int, double>::iterator it_end = pkt->req->getCacheStatsEnd();

        for (; it != it_end; it++) {
            std::pair<int, ContextID> key = std::make_pair(it->first, pkt->req->contextId());
            if (cache_stats.find(key) != cache_stats.end()) {
                if (cache_stats[key].first <= ((Counter) it->second) && cache_stats[key].second <= pkt->req->getInstCount()) {
                    // TODO: Should not update new statistics
                    cache_stats[key] = std::make_pair((Counter) it->second, pkt->req->getInstCount());
                }
            }
        }
    }
    if ((pkt->isResponse() || pkt->isRequest()) && pkt->req->hasDRAMStats()) {
        Counter temp_access = (Counter) pkt->req->getDRAMAccess();
        Counter temp_hit = (Counter) pkt->req->getDRAMRowHit();
        if (temp_access <= dram_stats[0] && temp_hit <= dram_stats[1]) {
            dram_stats[0] = temp_access;
            dram_stats[1] = temp_hit;
        }
    }

    // TODO: Only count requests?
    if (pkt->isRequest()) {
        cache_stats[std::make_pair(_cache_level, pkt->req->contextId())].second += 1;
        cache_stats[std::make_pair(_cache_level, pkt->req->contextId())].first += (!hit);
    }
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
    // TODO: Aging this seems to be quite strange in Flock
    // TODO: Ratio counter
    for (const auto& candidate : candidates) {
        std::shared_ptr<HawkeyeReplData> temp =
            std::static_pointer_cast<HawkeyeReplData>(candidate->replacementData);
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
        return;
    }

    int component_index;
    if (_cache_partition_on) {
        component_index = pkt->req->contextId();
    } else {
        component_index = 0;
    }

    DPRINTF(CacheRepl, "Cache hit ---- Packet type having PC: %s\n", pkt->cmdString());

    if (casted_replacement_data->is_cache_friendly) {
        casted_replacement_data->rrpv.reset();
    } else {
        casted_replacement_data->rrpv.saturate();
    }
    casted_replacement_data->context_id = pkt->req->contextId();

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_cache_sets) - 1);

    DPRINTF(CacheRepl, "Cache hit ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    // Warning: Timestamp is 8-bit integer in this design
    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;

    if (samplers[component_index]->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp)) {
        curr_timestamp = curr_timestamp % opt_vectors[component_index]->get_vector_size();

        DPRINTF(CacheRepl, "Cache hit ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictors[component_index]->train(last_PC, opt_vectors[component_index]->should_cache(curr_timestamp, last_timestamp));
        proj_vectors[component_index]->should_cache(curr_timestamp, last_timestamp);

        opt_vectors[component_index]->add_access(curr_timestamp);
        proj_vectors[component_index]->add_access(curr_timestamp);
    }
}

std::shared_ptr<ReplacementData> Hawkeye::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new HawkeyeReplData(_num_rrpv_bits));
}

void Hawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    if (!pkt->isRequest() || !pkt->req->hasPC() || !pkt->req->hasContextId()) {
        return;
    }

    int component_index;
    if (_cache_partition_on) {
        component_index = pkt->req->contextId();
    } else {
        component_index = 0;
    }

    DPRINTF(CacheRepl, "Cache miss handling ---- Packet type having PC: %s\n", pkt->cmdString());

    bool is_friendly = predictors[component_index]->predict(pkt->req->getPC());

    casted_replacement_data->is_cache_friendly = is_friendly;

    if (is_friendly) {
        casted_replacement_data->rrpv.saturate();
    } else {
        casted_replacement_data->rrpv.reset();
    }

    casted_replacement_data->valid = true;
    casted_replacement_data->context_id = pkt->req->contextId();

    DPRINTF(CacheRepl, "Cache miss handling ---- New Cache Line: Friendliness %d RRPV: %d Valid: %d\n", casted_replacement_data->is_cache_friendly, 
            casted_replacement_data->rrpv, casted_replacement_data->valid);

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_cache_sets) - 1);

    DPRINTF(CacheRepl, "Cache miss handling ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;

    if (samplers[component_index]->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp)) {
        curr_timestamp = curr_timestamp % opt_vectors[component_index]->get_vector_size();

        DPRINTF(CacheRepl, "Cache miss handling ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictors[component_index]->train(last_PC, opt_vectors[component_index]->should_cache(curr_timestamp, last_timestamp));
        proj_vectors[component_index]->should_cache(curr_timestamp, last_timestamp);

        opt_vectors[component_index]->add_access(curr_timestamp);
        proj_vectors[component_index]->add_access(curr_timestamp);
    }
}

void Hawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

void Hawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

double Hawkeye::getCurrFCP(int core_id) {
    // TODO: 
    //  1. I now have miss count for different levels with different contextID
    //  2. mr1, mr2, and mr3 can be calculated
    //  3. T2, T3, and Tdram are not quite sured (Can be fixed number or other numbers)
    return 0.0;
}

double Hawkeye::getProjFCP(int core_id) {
    // TODO:
    //  1. mr1, mr2 is the same as current FCP
    //  2. miss count under 10% higher partition can be linear interpolation based on current miss count, sampled projected miss count, sampled miss count
    //  3. T2, and T3 are not quite sured (Can be fixed number or other numbers)
    //  4. Tdram should based on rowhits, rowmisses, DRAM average latency, sampled projected miss count, sampled miss count, and current miss count
    return 0.0;
}

void Hawkeye::setNewPartition() {
    // TODO:
    //  Paper: Algorithm 1 Heuristic for Scalable Partitioning
}

void Hawkeye::setAgingCounter() {
    // TODO:
    //  Based on different cache access, set the aging counter
}

} // namespace replacement_policy
} // namespace gem5
