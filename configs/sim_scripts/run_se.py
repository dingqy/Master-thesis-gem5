"""
Run a simulation in syscall-emulation mode,
reporting separate statistics for an initial phase, the ROI,
and any remainder of execution. By default, terminates at ROI end.
"""
import os
import sys
import argparse
import m5
from m5.objects import *
from our_components.cores.cs395t_core import CS395T_CPU
from our_components.cores.util.cs395t_cpu_factory import CS395T_SimpleCore
from our_components.memory_hierarchies.memory_hierarchy import (
    CS395T_MemoryHierarchy
)
from gem5.utils.requires import requires
from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.simple_core import SimpleCore
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.components.processors.cpu_types import CPUTypes
from gem5.resources.resource import CustomResource
from gem5.simulate.simulator import Simulator
from gem5.simulate.exit_event import ExitEvent
from gem5.resources.resource import Resource

# parser = argparse.ArgumentParser(
#     description="Invoke a binary in SE mode with a specified ROI for stats collection"
# )
# parser.add_argument("input_bin", help="path to input binary")
# parser.add_argument('input_bin_args', nargs=argparse.REMAINDER)
# args = parser.parse_args()

requires(
    isa_required=ISA.X86,
)

# Setup the system memory
# (note: the X86 board supports only 3GB of main memory)
memory = DualChannelDDR4_2400(size="3GB")

# Set up the cache hierarchy
cache_hierarchy = CS395T_MemoryHierarchy(
   l1i_pref = "none",
   l1i_repl = "lru",
   l1d_pref = "none",
   l1d_repl = "lru",
   l2_pref = "none",
   l2_repl = "lru",
   llc_pref = "none",
   llc_repl = "hawkeye"
)

# This is hackish, but the SimpleProcessor models only understand the
# built-in CPU types, and we've created our own. Overload the method
# that AbstractCore uses to create new cores to make it use our own
# when "O3" is specified
SimpleCore.cpu_simobject_factory = CS395T_SimpleCore.cs395t_cpu_factory

# Set up the processor (which contains some number of cores)
# A switchable processor will start simulation in the first processor
# and switch to the second on a call to processor.switch()
processor = SimpleProcessor(
    cpu_type=CPUTypes.O3,
    isa=ISA.X86,
    num_cores=1
)

# Here we setup the board
board = SimpleBoard(
    clk_freq="4GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# # Set the workload to the user-specified binary
# if os.path.isabs(args.input_bin):
#     inbin = args.input_bin
# else:
#     inbin = os.path.join(os.getcwd(), args.input_bin);
# if not os.path.exists(inbin):
#     print("Input binary {} does not exist!".format(inbin))
#     sys.exit(1)
# binary = CustomResource(inbin)
binary = Resource("x86-hello64-static")
# board.set_se_binary_workload(binary, arguments=args.input_bin_args)
board.set_se_binary_workload(binary)

# Set up simulator
simulator = Simulator(
    board=board,
)

print("***Beginning simulation!")
simulator.run()

print(
    "***Exiting @ tick {} because {}.".format(
        simulator.get_current_tick(),
        simulator.get_last_exit_event_cause()
    )
)
