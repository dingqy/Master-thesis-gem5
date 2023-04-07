from m5.objects import *
from .base_caches import (
        CS395T_BaseL1Cache,
        CS395T_BaseL2Cache,
        CS395T_BaseLLCache
)

"""
For all cache options, see src/mem/cache/Cache.py class BaseCache
"""

""" L1 instruction cache """
class CS395T_L1ICache(CS395T_BaseL1Cache):
    size = '32kB'
    assoc = 8
    tag_latency = 4
    data_latency = 4
    mshrs = 8

    def __init__(self, pref : str, repl : str):
        print("Creating CS395T_L1ICache")
        super().__init__(pref, repl)
        self.demand_mshr_reserve = (CS395T_L1ICache.mshrs / 2);

    def connectCPU(self, cpu):
        """ Connect this cache's port to a CPU icache port """
        self.cpu_side = cpu.icache_port

""" L1 data cache """
class CS395T_L1DCache(CS395T_BaseL1Cache):
    size = "32kB"
    assoc = 8
    tag_latency = 4
    data_latency = 4
    mshrs = 16

    # write coalescer: optimizes streaming writes by coalescing
    # and then avoiding allocation in the current cache
    write_allocator = WriteAllocator()
    write_allocator.coalesce_limit = 2
    write_allocator.no_allocate_limit = 8
    write_allocator.delay_threshold = 8

    def __init__(self, pref : str, repl : str):
        print("Creating CS395T_L1DCache")
        super().__init__(pref, repl)
        self.demand_mshr_reserve = (CS395T_L1DCache.mshrs / 2);

    def connectCPU(self, cpu):
        """ Connect this cache's port to a CPU dcache port """
        self.cpu_side = cpu.dcache_port

""" L2 cache """
class CS395T_L2Cache(CS395T_BaseL2Cache):
    size = '1MB'
    assoc = 16
    tag_latency = 14
    data_latency = 14
    mshrs = 32

    def __init__(self, pref : str, repl : str):
        print("Creating CS395T_L2Cache")
        super().__init__(pref, repl)
        self.demand_mshr_reserve = (CS395T_L2Cache.mshrs / 2);

""" Last-level cache """
class CS395T_LLCache(CS395T_BaseLLCache):
    size = '8MB'
    assoc = 16
    tag_latency = 44
    data_latency = 44
    mshrs = 256

    def __init__(self, pref : str, repl : str):
        print("Creating CS395T_L3Cache")
        super().__init__(pref, repl)
        self.demand_mshr_reserve = (CS395T_LLCache.mshrs / 2);
