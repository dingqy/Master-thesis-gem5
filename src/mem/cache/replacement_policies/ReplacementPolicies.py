# Copyright (c) 2018-2020 Inria
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from m5.params import *
from m5.proxy import *
from m5.SimObject import SimObject


class BaseReplacementPolicy(SimObject):
    type = "BaseReplacementPolicy"
    abstract = True
    cxx_class = "gem5::replacement_policy::Base"
    cxx_header = "mem/cache/replacement_policies/base.hh"


class DuelingRP(BaseReplacementPolicy):
    type = "DuelingRP"
    cxx_class = "gem5::replacement_policy::Dueling"
    cxx_header = "mem/cache/replacement_policies/dueling_rp.hh"

    constituency_size = Param.Unsigned(
        "The size of a region containing one sample"
    )
    team_size = Param.Unsigned(
        "Number of entries in a sampling set that belong to a team"
    )
    replacement_policy_a = Param.BaseReplacementPolicy(
        "Sub-replacement policy A"
    )
    replacement_policy_b = Param.BaseReplacementPolicy(
        "Sub-replacement policy B"
    )


class FIFORP(BaseReplacementPolicy):
    type = "FIFORP"
    cxx_class = "gem5::replacement_policy::FIFO"
    cxx_header = "mem/cache/replacement_policies/fifo_rp.hh"


class SecondChanceRP(FIFORP):
    type = "SecondChanceRP"
    cxx_class = "gem5::replacement_policy::SecondChance"
    cxx_header = "mem/cache/replacement_policies/second_chance_rp.hh"


class LFURP(BaseReplacementPolicy):
    type = "LFURP"
    cxx_class = "gem5::replacement_policy::LFU"
    cxx_header = "mem/cache/replacement_policies/lfu_rp.hh"


class LRURP(BaseReplacementPolicy):
    type = "LRURP"
    cxx_class = "gem5::replacement_policy::LRU"
    cxx_header = "mem/cache/replacement_policies/lru_rp.hh"


class BIPRP(LRURP):
    type = "BIPRP"
    cxx_class = "gem5::replacement_policy::BIP"
    cxx_header = "mem/cache/replacement_policies/bip_rp.hh"
    btp = Param.Percent(3, "Percentage of blocks to be inserted as MRU")


class LIPRP(BIPRP):
    btp = 0


class MRURP(BaseReplacementPolicy):
    type = "MRURP"
    cxx_class = "gem5::replacement_policy::MRU"
    cxx_header = "mem/cache/replacement_policies/mru_rp.hh"


class RandomRP(BaseReplacementPolicy):
    type = "RandomRP"
    cxx_class = "gem5::replacement_policy::Random"
    cxx_header = "mem/cache/replacement_policies/random_rp.hh"


class BRRIPRP(BaseReplacementPolicy):
    type = "BRRIPRP"
    cxx_class = "gem5::replacement_policy::BRRIP"
    cxx_header = "mem/cache/replacement_policies/brrip_rp.hh"
    num_bits = Param.Int(2, "Number of bits per RRPV")
    hit_priority = Param.Bool(
        False, "Prioritize evicting blocks that havent had a hit recently"
    )
    btp = Param.Percent(
        3, "Percentage of blocks to be inserted with long RRPV"
    )


class RRIPRP(BRRIPRP):
    btp = 100


class DRRIPRP(DuelingRP):
    # The constituency_size and the team_size must be manually provided, where:
    #     constituency_size = num_cache_entries /
    #         (num_dueling_sets * num_entries_per_set)
    # The paper assumes that:
    #     num_dueling_sets = 32
    #     team_size = num_entries_per_set
    replacement_policy_a = BRRIPRP()
    replacement_policy_b = RRIPRP()


class NRURP(BRRIPRP):
    btp = 100
    num_bits = 1


class SHiPRP(BRRIPRP):
    type = "SHiPRP"
    abstract = True
    cxx_class = "gem5::replacement_policy::SHiP"
    cxx_header = "mem/cache/replacement_policies/ship_rp.hh"

    shct_size = Param.Unsigned(16384, "Number of SHCT entries")
    # By default any value greater than 0 is enough to change insertion policy
    insertion_threshold = Param.Percent(
        1, "Percentage at which an entry changes insertion policy"
    )
    # Always make hits mark entries as last to be evicted
    hit_priority = True
    # Let the predictor decide when to change insertion policy
    btp = 0


class SHiPMemRP(SHiPRP):
    type = "SHiPMemRP"
    cxx_class = "gem5::replacement_policy::SHiPMem"
    cxx_header = "mem/cache/replacement_policies/ship_rp.hh"


class SHiPPCRP(SHiPRP):
    type = "SHiPPCRP"
    cxx_class = "gem5::replacement_policy::SHiPPC"
    cxx_header = "mem/cache/replacement_policies/ship_rp.hh"


class TreePLRURP(BaseReplacementPolicy):
    type = "TreePLRURP"
    cxx_class = "gem5::replacement_policy::TreePLRU"
    cxx_header = "mem/cache/replacement_policies/tree_plru_rp.hh"
    num_leaves = Param.Int(Parent.assoc, "Number of leaves in each tree")


class WeightedLRURP(LRURP):
    type = "WeightedLRURP"
    cxx_class = "gem5::replacement_policy::WeightedLRU"
    cxx_header = "mem/cache/replacement_policies/weighted_lru_rp.hh"


class HawkeyeRP(BaseReplacementPolicy):
    """
    Parameters:
        1. num_rrpv_bits (RRPV bits)
        2. num_cache_sets (Number of target cache sets)
        3. cache_block_size (Number of target cache block size)
        4. num_cache_ways (Number of target cache ways)
        5. optgen_vector_size (The size of occupancy vector)
        6. num_pred_entries (Number of predictor entries)
        7. pred_num_bits_per_entry (Number of counter bits per entry in predictor)
        8. num_sampled_sets (Number of sets in sampled cache)
        9. timer_size (Number of bits for timestamp)
        10. cache_partition_on (Enable cache parition enforcement mechanism)
    """

    type = "HawkeyeRP"
    cxx_class = "gem5::replacement_policy::Hawkeye"
    cxx_header = "mem/cache/replacement_policies/hawkeye_rp.hh"

    num_rrpv_bits = Param.Int(3, "Number of bits per RRPV")
    num_cache_sets = Param.Int(16384, "Number of target cache sets")
    cache_block_size = Param.Int(64, "Number of target cache block size")
    num_cache_ways = Param.Int(16, "Number of target cache ways")
    optgen_vector_size = Param.Int(128, "The size of occupancy vector")
    num_pred_entries = Param.Int(8192, "Number of predictor entries")
    num_pred_bits = Param.Int(
        3, "Number of counter bits per entry in predictor"
    )
    num_sampled_sets = Param.Int(300, "Number of sets in sampled cache")
    timer_size = Param.Int(1 << 10, "Number of bits for timestamp")
    cache_partition_on = Param.Bool(False, "Enable cache partition enforcement mechanism")
    num_cpus = Param.Int(1, "Number of CPU cores")
    cache_level = Param.Int(1, "Cache level")

class MockingjayRP(BaseReplacementPolicy):
    """
    Parameters:
        1. num_etr_bits (ETR bits)
        2. num_cache_sets (Number of target cache sets)
        3. cache_block_size (Number of target cache block size)
        4. num_cache_ways (Number of target cache ways)
        5. num_cpu (The number of cores)
        6. num_pred_entries (Number of predictor entries)
        7. pred_num_bits_per_entry (Number of counter bits per entry in predictor)
        8. num_sampled_sets (Number of sets in sampled cache)
        9. timer_size (Number of bits for timestamp)
        10. cache_partition_on (Enable cache parition enforcement mechanism)
    """

    type = "MockingjayRP"
    cxx_class = "gem5::replacement_policy::Mockingjay"
    cxx_header = "mem/cache/replacement_policies/mockingjay_rp.hh"

    num_etr_bits = Param.Int(3, "Number of bits per ETR")
    num_cache_sets = Param.Int(16384, "Number of target cache sets")
    cache_block_size = Param.Int(64, "Number of target cache block size")
    num_cache_ways = Param.Int(16, "Number of target cache ways")
    num_pred_entries = Param.Int(1 << 11, "Number of predictor entries")
    num_pred_bits = Param.Int(
        7, "Number of counter bits per entry in predictor"
    )
    num_sampled_sets = Param.Int(512, "Number of sets in sampled cache")
    timer_size = Param.Int(8, "Number of bits for timestamp")
    num_cpus = Param.Int(1, "Number of cores")
    num_clock_bits = Param.Int(3, "Number of bits for aging clock")
    cache_partition_on = Param.Bool(False, "Enable cache partition enforcement mechanism")
    