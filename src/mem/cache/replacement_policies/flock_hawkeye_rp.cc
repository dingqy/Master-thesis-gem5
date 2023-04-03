#include "mem/cache/replacement_policies/flock_hawkeye_rp.hh"

#include "base/logging.hh"
#include "params/FlockHawkeyeRP.hh"
#include "debug/CacheRepl.hh"
#include "base/trace.hh"

namespace gem5
{

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{


FlockHawkeye::FlockHawkeye(const Params &p) : Base(p), _num_rrpv_bits(p.num_rrpv_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_cache_sets((int) std::log2(p.num_cache_sets)),
                                    _num_cpus(p.num_cpus), _num_cache_ways(p.num_cache_ways), _cache_level(p.cache_level), dram_latency(0) {
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
    
    dram_stats[0] = 0;
    dram_stats[1] = 0;
    
    for (int i = 0; i < p.num_cpus; i++) {
        samplers.push_back(std::make_unique<HistorySampler>(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size));
        predictors.push_back(std::make_unique<PCBasedPredictor>(p.num_pred_entries, p.num_pred_bits));
        opt_vectors.push_back(std::make_unique<OccupencyVector>(p.num_cache_ways, p.optgen_vector_size));
        proj_vectors.push_back(std::make_unique<OccupencyVector>(p.num_cache_ways, p.optgen_vector_size));
    }

    curr_partition.resize(p.num_cpus, 0);
    ratio_counter.resize(p.num_cpus);

    for (int i = 0; i < p.num_cpus; i++) {
        cache_stats[std::make_pair(p.cache_level, i)] = std::make_pair(0, 0);
    }

    DPRINTF(CacheRepl, "Cache Initialization ---- Number of Cache Sets: %d, Cache Block Size: %d, Number of Cache Ways: %d\n", p.num_cache_sets, p.cache_block_size, p.num_cache_ways);
    DPRINTF(CacheRepl, "History Sampler Initialization ---- Number of Sample Sets: %d, Timer Size: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(CacheRepl, "Occupancy Vector Initialization ---- Vector size: %d\n", p.optgen_vector_size);
    DPRINTF(CacheRepl, "Predictor Initialization ---- Number of Predictor Entries: %d, Counter of Predictors: %d\n", p.num_pred_entries, p.num_pred_bits);
}

void FlockHawkeye::invalidate(const std::shared_ptr<ReplacementData> &replacement_data)
{
    std::shared_ptr<FlockHawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<FlockHawkeyeReplData>(replacement_data);

    // Invalidate entry

    // TODO: If it is sampled cache line, then that cache line should be invalidated also.
    casted_replacement_data->valid = false;
    casted_replacement_data->is_cache_friendly = false;
}

void FlockHawkeye::access(const PacketPtr pkt, bool hit, const ReplacementCandidates& candidates) {
    if (pkt->isRequest() && pkt->req->hasCacheStats()) {
        std::unordered_map<int, std::pair<double, double>>::iterator it = pkt->req->getCacheStatsBegin();
        std::unordered_map<int, std::pair<double, double>>::iterator it_end = pkt->req->getCacheStatsEnd();

        for (; it != it_end; it++) {
            std::pair<int, ContextID> key = std::make_pair(it->first, pkt->req->contextId());
            if (cache_stats.find(key) != cache_stats.end()) {
                if (cache_stats[key].first <= ((Counter) it->second.first) && cache_stats[key].second <= pkt->req->getInstCount()) {
                    // TODO: Should not update new statistics
                    cache_stats[key] = std::make_pair((Counter) it->second.first, pkt->req->getInstCount());
                    cache_latency_stats[key] = it->second.second;
                }
            }
        }
    }

    if (pkt->isResponse() && pkt->req->hasDRAMStats()) {
        Counter temp_access = (Counter) pkt->req->getDRAMAccess();
        Counter temp_hit = (Counter) pkt->req->getDRAMRowHit();
        if (temp_access <= dram_stats[0] && temp_hit <= dram_stats[1]) {
            dram_stats[0] = temp_access;
            dram_stats[1] = temp_hit;
            dram_latency = pkt->req->getAccessLatency();
            dram_ready = true;
        }
    }

    // TODO: Only count requests?
    // Miss count + Access count (This is different from higher level caches)
    if (pkt->isRequest()) {
        cache_stats[std::make_pair(_cache_level, pkt->req->contextId())].second += 1;
        cache_stats[std::make_pair(_cache_level, pkt->req->contextId())].first += (!hit);
    }

    if (pkt->isRequest() && pkt->req->hasContextId() && pkt->req->hasInstCount() && pkt->req->hasNumCycles()) {
        cpi_stats[pkt->req->contextId()] = pkt->req->getNumCycles() / pkt->req->getInstCount();
    }

    for (int i = 0; i < _num_cpus; i++) {
        if (ratio_counter[i].counter >= ratio_counter[i].ratio_max) {
            for (const auto& candidate : candidates) {
                std::shared_ptr<FlockHawkeyeReplData> candidate_repl_data =
                    std::static_pointer_cast<FlockHawkeyeReplData>(
                        candidate->replacementData);
                // TODO: Is saturated counter support comparison?
                if (candidate_repl_data->valid && candidate_repl_data->context_id == i && candidate_repl_data->rrpv < 6) {
                    candidate_repl_data->rrpv++;
                }
            }
            ratio_counter[i].counter = 0;   
        } else {
            ratio_counter[i].counter += 1;
        }
    }
}

ReplaceableEntry* FlockHawkeye::getVictim(const ReplacementCandidates& candidates) const {
    // There must be at least one replacement candidate
    assert(candidates.size() > 0);

    // Use first candidate as dummy victim
    ReplaceableEntry* victim = candidates[0];

    // Store victim->rrpv in a variable to improve code readability
    int victim_RRPV = std::static_pointer_cast<FlockHawkeyeReplData>(
                        victim->replacementData)->rrpv;

    // Visit all candidates to find victim
    // If there is no invalid cache line, the one with highest RRPV will be evicted
    // TODO: Bypass cache should be possible (return nullptr)
    for (const auto& candidate : candidates) {
        std::shared_ptr<FlockHawkeyeReplData> candidate_repl_data =
            std::static_pointer_cast<FlockHawkeyeReplData>(
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
        std::shared_ptr<FlockHawkeyeReplData> temp =
            std::static_pointer_cast<FlockHawkeyeReplData>(candidate->replacementData);
        // TODO: Is saturated counter support comparison?
        if (temp->valid && temp->is_cache_friendly && temp->rrpv < 6) {
            temp->rrpv++;
        }
        panic_if(temp->rrpv > 6 && temp->is_cache_friendly, "Friendly cache should never be the maximum value of RRPV (6)");
    }

    return victim;
}

void FlockHawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {
    std::shared_ptr<FlockHawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<FlockHawkeyeReplData>(replacement_data);

    // TODO: Which requests should we monitor?
    if (!pkt->isRequest() || !pkt->req->hasPC() || !pkt->req->hasContextId()) {
        return;
    }

    int component_index = pkt->req->contextId();

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

std::shared_ptr<ReplacementData> FlockHawkeye::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new FlockHawkeyeReplData(_num_rrpv_bits));
}

void FlockHawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    std::shared_ptr<FlockHawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<FlockHawkeyeReplData>(replacement_data);

    if (!pkt->isRequest() || !pkt->req->hasPC() || !pkt->req->hasContextId()) {
        return;
    }

    int component_index = pkt->req->contextId();

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

void FlockHawkeye::reset(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train FlockHawkeye's predictor without access information.");
}

void FlockHawkeye::touch(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train FlockHawkeye's predictor without access information.");
}

double FlockHawkeye::getCurrFCP(int core_id) {
    // TODO: 
    //  1. I now have miss count for different levels with different contextID
    //  2. mr1, mr2, and mr3 can be calculated
    //  3. T2, T3, and Tdram are not quite sured (Can be fixed number or other numbers)
    // Level 0 and Level 1
    std::pair<int, ContextID> key_l1i = std::make_pair(0, core_id);
    std::pair<int, ContextID> key_l1d = std::make_pair(1, core_id);
    if (cache_stats.find(key_l1i) == cache_stats.end() || cache_stats.find(key_l1d) == cache_stats.end()) {
        return -1.0;
    }
    // TODO: How to combine L1I and L1D
    Counter misses_l1 = cache_stats[key_l1i].first + cache_stats[key_l1d].first;
    Counter inst_l1 = std::max(cache_stats[key_l1i].second, cache_stats[key_l1d].second);
    double mr1 = ((double) misses_l1) / ((double) inst_l1);

    // Level 2
    std::pair<int, ContextID> key_l2 = std::make_pair(2, core_id);
    if (cache_stats.find(key_l2) == cache_stats.end()) {
        return -1.0;
    }
    Counter misses_l2 = cache_stats[key_l2].first + cache_stats[key_l2].first;
    Counter inst_l2 = cache_stats[key_l2].second;
    double mr2 = ((double) misses_l2) / ((double) inst_l2);
    
    // Level 3
    std::pair<int, ContextID> key_l3 = std::make_pair(_cache_level, core_id);
    if (cache_stats.find(key_l3) == cache_stats.end()) {
        return -1.0;
    }
    Counter misses_l3 = cache_stats[key_l3].first + cache_stats[key_l3].first;
    Counter inst_l3 = cache_stats[key_l3].second;
    double mr3 = ((double) misses_l3) / ((double) inst_l3);

    // DRAM
    if (!dram_ready) {
        return -1.0;
    }

    gem5_assert(mr1 >= mr2, "Miss rate difference can not be negative");
    gem5_assert(mr2 >= mr3, "Miss rate difference can not be negative");
    double fcp = (mr1 - mr2) * cache_latency_stats[key_l2] + (mr2 - mr3) * cache_latency_stats[key_l3] + mr3 * dram_latency;
    
    return fcp;
}

double FlockHawkeye::getProjFCP(int core_id) {
    // TODO:
    //  1. mr1, mr2 is the same as current FCP
    //  2. miss count under 10% higher partition can be linear interpolation based on current miss count, sampled projected miss count, sampled miss count
    //  3. T2, and T3 are not quite sured (Can be fixed number or other numbers)
    //  4. Tdram should based on rowhits, rowmisses, DRAM average latency, sampled projected miss count, sampled miss count, and current miss count


    // Level 0 and Level 1
    std::pair<int, ContextID> key_l1i = std::make_pair(0, core_id);
    std::pair<int, ContextID> key_l1d = std::make_pair(1, core_id);
    if (cache_stats.find(key_l1i) == cache_stats.end() || cache_stats.find(key_l1d) == cache_stats.end()) {
        return -1.0;
    }
    // TODO: How to combine L1I and L1D
    Counter misses_l1 = cache_stats[key_l1i].first + cache_stats[key_l1d].first;
    Counter inst_l1 = std::max(cache_stats[key_l1i].second, cache_stats[key_l1d].second);
    double mr1 = ((double) misses_l1) / ((double) inst_l1);

    // Level 2
    std::pair<int, ContextID> key_l2 = std::make_pair(2, core_id);
    if (cache_stats.find(key_l2) == cache_stats.end()) {
        return -1.0;
    }
    Counter misses_l2 = cache_stats[key_l2].first + cache_stats[key_l2].first;
    Counter inst_l2 = cache_stats[key_l2].second;
    double mr2 = ((double) misses_l2) / ((double) inst_l2);
    
    // Level 3
    std::pair<int, ContextID> key_l3 = std::make_pair(_cache_level, core_id);
    if (cache_stats.find(key_l3) == cache_stats.end()) {
        return -1.0;
    }
    Counter misses_l3 = cache_stats[key_l3].first + cache_stats[key_l3].first;
    Counter inst_l3 = cache_stats[key_l3].second;
    double mr3 = ((double) misses_l3) / ((double) inst_l3);

    double frac = proj_vectors[core_id]->get_num_opt_misses(proj_vectors[core_id]->getCacheSize()) / opt_vectors[core_id]->get_num_opt_misses(opt_vectors[core_id]->getCacheSize());
    double mr3_proj = frac * mr3;

    // DRAM
    if (!dram_ready) {
        return -1.0;
    }
    double est_miss_count = mr3_proj * inst_l3;
    double dram_latency_proj = ((dram_stats[0] - dram_stats[1]) / dram_stats[1]) * (est_miss_count / (double) misses_l3) * dram_latency;
    
    gem5_assert(mr1 >= mr2, "Miss rate difference can not be negative");
    gem5_assert(mr2 >= mr3, "Miss rate difference can not be negative");
    double fcp = (mr1 - mr2) * cache_latency_stats[key_l2] + (mr2 - mr3_proj) * cache_latency_stats[key_l3] + mr3_proj * dram_latency_proj;
    
    return fcp;
}

void FlockHawkeye::setNewPartition() {
    // TODO:
    //  Paper: Algorithm 1 Heuristic for Scalable Partitioning

    int total_credit = 0;
    for (auto &i : curr_partition) {
        total_credit += i;
    }    
    if (total_credit > 0) {
        int max_core_id = -1;
        double max_gain = 0.0;
        for (int i = 0; i < _num_cpus; i++) {
            int curr_idx_partition = curr_partition[i];
            int gain = (getProjFCP(i) - getCurrFCP(i)) / cpi_stats[i];
            if (gain > max_gain) {
                max_gain = gain;
                max_core_id = i;
            }
        }
        
        gem5_assert(max_core_id != 1, "If there is partition budget, it should be dispatched");
        int new_partition = curr_partition[max_core_id] + std::floor(0.1 * _num_cache_ways);
        curr_partition[max_core_id] = new_partition;
        opt_vectors[max_core_id]->setCacheSize(curr_partition[max_core_id]);

        // TODO: Allow reduce partition size
        proj_vectors[max_core_id]->setCacheSize(std::min(_num_cache_ways, new_partition));
    }
}

void FlockHawkeye::setAgingCounter() {
    // TODO:
    //  Based on different cache access, set the aging counter
    int min_access_idx = -1;
    Counter min_access = 0;

    for (int i = 0; i < _num_cpus; i++) {
        std::pair<int, ContextID> key = std::make_pair(_cache_level, i);
        if (cache_stats[key].second != 0 && cache_stats[key].second > min_access) {
            min_access_idx = i;
            min_access = cache_stats[key].second;
        }
    }

    if (min_access_idx != -1) {
        for (int i = 0; i < _num_cpus; i++) {
            std::pair<int, ContextID> key = std::make_pair(_cache_level, i);
            if (cache_stats[key].second != 0) {
                ratio_counter[i].ratio_max = cache_stats[key].second / min_access - 1;
                gem5_assert(ratio_counter[i].ratio_max >= 0, "Ratio counter cannot be negative");
            }
        }
    }

}

} // namespace replacement_policy
} // namespace gem5
