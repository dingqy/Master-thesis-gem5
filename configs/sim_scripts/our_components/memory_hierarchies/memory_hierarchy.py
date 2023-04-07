from m5.objects import L2XBar, SystemXBar, BadAddr, SnoopFilter, Cache
from gem5.components.cachehierarchies.classic.abstract_classic_cache_hierarchy import (
    AbstractClassicCacheHierarchy
)
from gem5.components.boards.abstract_board import AbstractBoard
from gem5.utils.override import *

from .caches.cs395t_caches import (
    CS395T_L1DCache,
    CS395T_L1ICache,
    CS395T_L2Cache,
    CS395T_LLCache
)
from .caches.base_caches import CS395T_MMUCache

class CS395T_MemoryHierarchy(AbstractClassicCacheHierarchy):
    def __init__(self, l1i_pref: str, l1i_repl: str, 
                       l1d_pref: str, l1d_repl: str, 
                       l2_pref: str, l2_repl: str, 
                       llc_pref: str, llc_repl: str):
        print("Creating CS395T_MemoryHierarchy")
        super().__init__()
        self.membus = SystemXBar(width=192)
        self.membus.badaddr_responder = BadAddr()
        self.membus.default = self.membus.badaddr_responder.pio
        self._l1i_pref = l1i_pref
        self._l1i_repl = l1i_repl
        self._l1d_pref = l1d_pref
        self._l1d_repl = l1d_repl
        self._l2_pref = l2_pref
        self._l2_repl = l2_repl
        self._llc_pref = llc_pref
        self._llc_repl = llc_repl

    @overrides(AbstractClassicCacheHierarchy)
    def get_mem_side_port(self):
        return self.membus.mem_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def get_cpu_side_port(self):
        return self.membus.cpu_side_ports

    @overrides(AbstractClassicCacheHierarchy)
    def incorporate_cache(self, board: AbstractBoard):
        board.connect_system_port(self.membus.cpu_side_ports)

        for cntr in board.get_memory().get_memory_controllers():
            cntr.port = self.membus.mem_side_ports

        # Create L1s private to each core
        self.l1icaches = [
            CS395T_L1ICache(self._l1i_pref, self._l1i_repl)
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l1dcaches = [
            CS395T_L1DCache(self._l1d_pref, self._l1d_repl)
            for i in range(board.get_processor().get_num_cores())
        ]

        # Create L2s private to each core
        # and the buses to connect their L1s
        self.l2caches = [
            CS395T_L2Cache(self._l2_pref, self._l2_repl)
            for i in range(board.get_processor().get_num_cores())
        ]
        self.l2buses = [
            L2XBar(width=192)
            for i in range(board.get_processor().get_num_cores())
        ]

        # Each TLB is backed by a page table entry cache
        # (we're emulating a second-level TLB)
        self.iptw_caches = [
            CS395T_MMUCache()
            for i in range(board.get_processor().get_num_cores())
        ]
        self.dptw_caches = [
            CS395T_MMUCache()
            for i in range(board.get_processor().get_num_cores())
        ]

        # Create an LLC to be shared by all cores
        # and a coherent crossbar for its bus
        self.llcache = CS395T_LLCache(self._llc_pref, self._llc_repl);
        self.llcXbar = L2XBar(width=192, 
                              snoop_filter = SnoopFilter(max_capacity='32MB'))

        if board.has_coherent_io():
            self._setup_io_cache(board)

        for i, cpu in enumerate(board.get_processor().get_cores()):
            # connect each CPU to its L1 caches
            cpu.connect_icache(self.l1icaches[i].cpu_side)
            cpu.connect_dcache(self.l1dcaches[i].cpu_side)

            # connect each L1 caches to the bus to its L2
            self.l1icaches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.l1dcaches[i].mem_side = self.l2buses[i].cpu_side_ports

            # connect each PTE cache to the bus to its L2
            self.iptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports
            self.dptw_caches[i].mem_side = self.l2buses[i].cpu_side_ports

            # connect each L2 bus to its L2 cache
            self.l2buses[i].mem_side_ports = self.l2caches[i].cpu_side

            # connect each L2 cache to the crossbar to the shared LLC
            self.l2caches[i].mem_side = self.llcXbar.cpu_side_ports

            # connect the core's page-table walkers to the MMU caches
            cpu.connect_walker_ports(
                self.iptw_caches[i].cpu_side, self.dptw_caches[i].cpu_side
            )

            # connect interrupt port
            int_req_port = self.membus.mem_side_ports
            int_resp_port = self.membus.cpu_side_ports
            cpu.connect_interrupt(int_req_port, int_resp_port)

        # connect the LLC crossbar to the shared LLC
        self.llcXbar.mem_side_ports = self.llcache.cpu_side

        # connect the LLC to the memory bus
        self.membus.cpu_side_ports = self.llcache.mem_side

    """ Create a cache for coherent I/O connections """
    def _setup_io_cache(self, board: AbstractBoard):
        self.iocache = Cache(
            size="1kB",
            assoc=8,
            tag_latency=50,
            data_latency=50,
            response_latency=50,
            mshrs=20,
            tgts_per_mshr=12,
            addr_ranges=board.mem_ranges
        )
        self.iocache.mem_side = self.membus.cpu_side_ports
        self.iocache.cpu_side = board.get_mem_side_coherent_io_port()
