"""
Classic cache model caches

For all cache options, see src/mem/cache/Cache.py class BaseCache
"""
from m5.objects import *
from .configurable_cache import CS395T_ConfigurableCache

"""
An L1 configurable cache with some default values to align
  with ChampSim
Note that some values in the Cache object have no specified default
  and _must_ be given a default value in any child object that will
  actually be instantiated!
This object is meant to be extended for I-side and D-side caches,
  so we don't bother defining them all here
"""
class CS395T_BaseL1Cache(CS395T_ConfigurableCache):
    response_latency = 1
    tgts_per_mshr = 16
    write_buffers = 64 # matched to ChampSim default

    def connectBus(self, bus):
        """ Connect this cache to a memory-side bus """
        self.mem_side = bus.cpu_side_ports

    def connectCPU(self, cpu):
        """ Connect this cache's port to a CPU-side port:
           Must be done in sub-class! """
        raise NotImplementedError

""" 
Page table entry cache 
We're using this to approximate a 2-level TLB structure by at least
accelerating table walks
Skylake STLB: 1536 entries, 12-way set associative (128 sets)
"""
class CS395T_MMUCache(Cache):
    size = '8kB'
    assoc = 8
    tag_latency = 1
    data_latency = 1
    response_latency = 1
    mshrs = 8
    tgts_per_mshr = 8

    def __init__(self, **kwargs):
        print("Creating CS395T_MMUCache")
        super().__init__(**kwargs)

    def connectCPU(self, cpu):
        """ Connect the CPU I-TLB and D-TLB to the cache """
        self.mmubus = L2XBar()
        self.cpu_side = self.mmubus.mem_side_ports
        for tlb in [cpu.itb, cpu.dtb]:
            self.mmubus.cpu_side_ports = tlb.walker.port

    def connectBus(self, bus):
        """ Connectto a memory-side bus """
        self.mem_side = bus.cpu_side_ports

""" 
An L2 configurable cache with some default values to align
  with ChampSim
"""
class CS395T_BaseL2Cache(CS395T_ConfigurableCache):
    response_latency = 1
    tgts_per_mshr = 16
    write_buffers = 32 # matched to ChampSim default

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports

    def connectMemSideBus(self, bus):
        self.mem_side = bus.cpu_side_ports

""" 
A last-level configurable cache with some default values to align
  with Champsim
"""
class CS395T_BaseLLCache(CS395T_ConfigurableCache):
    response_latency = 1
    tgts_per_mshr = 32
    write_buffers = 128 # matched to ChampSim default for 4 cores

    # (note that normally in gem5, an LLC would be clusivity=mostly_excl;
    # but to match Champsim's mostly_incl behavior, leaving default)

    def connectCPUSideBus(self, bus):
        self.cpu_side = bus.mem_side_ports
    
    def connectMemSideBus(self, bus):
         self.mem_side = bus.cpu_side_ports
