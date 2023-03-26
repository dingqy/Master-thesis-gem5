#include "mem/cache/replacement_policies/mockingjay_rp.hh"
#include "base/logging.hh" // For fatal_if
#include "params/MockingjayRP.hh"

namespace gem5 {

GEM5_DEPRECATED_NAMESPACE(ReplacementPolicy, replacement_policy);
namespace replacement_policy
{

constexpr uint32_t MAX_RD = 104;

/**
 *  Parameters:
 *    1. num_etr_bits (ETR bits)
 *    2. num_cache_sets (Number of target cache sets)
 *    3. cache_block_size (Number of target cache block size)
 *    4. num_cache_ways (Number of target cache ways)
 *    5. num_cpu (Number of cores)
 *    6. num_pred_entries (Number of predictor entries)
 *    7. num_pred_bits (Number of counter bits per entry in predictor)
 *    8. num_sampled_sets (Number of sets in sampled cache)
 *    9. timer_size (Number of bits for timestamp)
 */
Mockingjay::Mockingjay(const Params &p) : Base(p), _num_etr_bits(p.num_etr_bits), _log2_block_size((int) std::log2(p.cache_block_size)), _log2_num_sets((int) std::log2(p.num_cache_sets)), _num_clock_bits(p.num_clock_bits) {
    sampled_cache = new SampledCache(p.num_sampled_sets, p.num_cache_sets, p.cache_block_size, p.timer_size, p.num_cpu);
    predictor = new ReuseDistPredictor(p.num_pred_entries, p.num_pred_bits);
    age_ctr = new uint8_t[p.num_cache_sets];

    DPRINTF(CacheRepl, "Cache Initialization ---- Number of Cache Sets: %d, Cache Block Size: %d, Number of Cache Ways: %d\n", p.num_cache_sets, p.cache_block_size, p.num_cache_ways);
    DPRINTF(CacheRepl, "History Sampler Initialization ---- Number of Sample Sets: %d, Timer Size: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(CacheRepl, "Predictor Initialization ---- Number of Predictor Entries: %d, Counter of Predictors: %d\n", p.num_pred_entries, p.num_pred_bits);
    DPRINTF(CacheRepl, "CPU Core Initialization ---- Number of Cores: %d\n", p.num_cpu);
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
    int victim_etr = std::static_pointer_cast<MockingjayReplData>(victim->replacementData)->etr;
    int abs_victim_etr = std::abs(victim_etr);

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
        int abs_candidate_etr = std::abs(candidate_etr);
        if (abs_candidate_etr > abs_victim_etr || ((abs_candidate_etr == abs_victim_etr) && (candidate_etr < 0))) {
            victim = candidate;
            victim_etr = candidate_etr;
            abs_victim_etr = abs_candidate_etr;
        }
    }

    // TODO: It can bypass the cache (Should be cared in reset since this interface cannot know which packet it belongs to)

    return victim;
}

void Mockingjay::touch(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt, const ReplacementCandidates& candidates) {
    std::shared_ptr<MockingjayReplData> casted_replacement_data =
        std::static_pointer_cast<MockingjayReplData>(replacement_data);

    // TODO: Which requests should we monitor?
    if (!pkt->req->hasPC()) {
        return;
    }

    DPRINTF(CacheRepl, "Cache hit ---- Packet type having PC: %s\n", pkt->cmdString());

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_sets) - 1);

    DPRINTF(CacheRepl, "Cache hit ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    uint64_t aging_max = (1 << _num_clock_bits) - 1;
    if (age_ctr[set] != aging_max) {
        age_ctr[set] += 1;
        return;
    }

    age_ctr[set] = 0;

    for (const auto &candidate : candidates) {
        std::shared_ptr<MockingjayReplData> candidate_repl_data =
            std::static_pointer_cast<MockingjayReplData>(
                candidate->replacementData);
        candidate_repl_data->aging();
    }

    // Warning: Timestamp is 8-bit integer in this design
    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;
    bool evict;

    if (sampled_cache->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp, true, &evict)) {
        curr_timestamp = curr_timestamp % opt_vector->get_vector_size();

        DPRINTF(CacheRepl, "Cache hit ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictor->train(last_PC, opt_vector->should_cache(curr_timestamp, last_timestamp));
    }
}

std::shared_ptr<ReplacementData> Mockingjay::instantiateEntry() {
    return std::shared_ptr<ReplacementData>(new HawkeyeReplData(_num_rrpv_bits));
}

void Mockingjay::reset(const std::shared_ptr<ReplacementData>& replacement_data, const PacketPtr pkt) {

    std::shared_ptr<HawkeyeReplData> casted_replacement_data =
        std::static_pointer_cast<HawkeyeReplData>(replacement_data);

    if (!pkt->req->hasPC()) {
        return;
    }

    DPRINTF(CacheRepl, "Cache miss handling ---- Packet type having PC: %s\n", pkt->cmdString());

    bool is_friendly = predictor->predict(pkt->req->getPC());

    casted_replacement_data->is_cache_friendly = is_friendly;

    if (is_friendly) {
        casted_replacement_data->rrpv.saturate();
    } else {
        casted_replacement_data->rrpv.reset();
    }

    casted_replacement_data->valid = true;

    DPRINTF(CacheRepl, "Cache miss handling ---- New Cache Line: Friendliness %d RRPV: %d Valid: %d\n", casted_replacement_data->is_cache_friendly, 
            casted_replacement_data->rrpv, casted_replacement_data->valid);

    int set = (pkt->getAddr() >> _log2_block_size) & ((1 << _log2_num_sets) - 1);

    DPRINTF(CacheRepl, "Cache miss handling ---- Request Address: 0x%.8x, Set Index: %d, PC: 0x%.8x\n", pkt->getAddr(), set, pkt->req->getPC());

    uint8_t curr_timestamp;
    uint8_t last_timestamp;
    uint16_t last_PC;

    if (sampler->sample(pkt->getAddr(), pkt->req->getPC(), &curr_timestamp, set, &last_PC, &last_timestamp)) {
        curr_timestamp = curr_timestamp % opt_vector->get_vector_size();

        DPRINTF(CacheRepl, "Cache miss handling ---- Sampler Hit, Last timestamp: %d, Current timestamp: %d, Last PC: %d\n", last_timestamp, curr_timestamp, last_PC);

        // sample hit
        predictor->train(last_PC, opt_vector->should_cache(curr_timestamp, last_timestamp));

        opt_vector->add_access(curr_timestamp);
    }
}

void Mockingjay::reset(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

void Mockingjay::touch(const std::shared_ptr<ReplacementData>& replacement_data) const {
    panic("Cant train Hawkeye's predictor without access information.");
}

}
}