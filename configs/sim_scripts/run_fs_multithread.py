"""
Run a simulation in full-system mode with N cores (default=2),
beginning execution on KVM (or atomic) cores and switching
to TIMING (or O3) beginning at the ROI workbegin in the benchmark.
"""
import os
import sys
import time
import argparse
import m5
from pathlib import Path
from m5.objects import *
from our_components.cores.cs395t_core import CS395T_CPU
from our_components.cores.util.cs395t_cpu_factory import CS395T_SimpleCore
from our_components.memory_hierarchies.memory_hierarchy import (
    CS395T_MemoryHierarchy
)
from gem5.utils.requires import requires
from gem5.components.boards.x86_board import X86Board
from gem5.components.memory import DualChannelDDR4_2400
from gem5.components.processors.simple_core import SimpleCore
from gem5.components.processors.simple_switchable_processor import SimpleSwitchableProcessor
from gem5.isas import ISA
from gem5.components.processors.cpu_types import CPUTypes
from gem5.resources.resource import Resource
from gem5.resources.resource import CustomDiskImageResource
from gem5.simulate.simulator import Simulator
from gem5.simulate.exit_event import ExitEvent

benchmark_choices = [ # GAP
                      "bc", "bfs", "cc", "pr", "sssp", "tc",
                      # Parsec
                      "blackscholes", "bodytrack", "canneal", "dedup", "facesim",
                      "ferret", "fluidanimate", "freqmine", "raytrace", 
                      "streamcluster", "swaptions", "vips", "x264"
                    ]
size_choices = [ "small", "medium", "large" ]

parser = argparse.ArgumentParser(
    description="Invoke a multithreaded GAP or Parsec benchmark in FS mode with N cores (default N=2)"
)
parser.add_argument("--benchmark", required=True, type=str, help="benchmark to run", choices=benchmark_choices)
parser.add_argument("--size", required=True, type=str, help="input size (small, medium, large)", choices=size_choices)
parser.add_argument("--cores", type=int, default=2, help="number of cores")
parser.add_argument("--nokvm", default=False, action='store_true', help="use atomic core for fast-forwarding instead of KVM")
parser.add_argument("--o3", default=False, action='store_true', help="use O3 core for ROI instead of Timing")
args = parser.parse_args()

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
   llc_repl = "lru"
)

# This is hackish, but the SimpleProcessor models only understand the
# built-in CPU types, and we've created our own. Overload the method
# that AbstractCore uses to create new cores to make it use our own
# when "O3" is specified
SimpleCore.cpu_simobject_factory = CS395T_SimpleCore.cs395t_cpu_factory

# Set up the processor.
processor = SimpleSwitchableProcessor(
    starting_core_type = CPUTypes.KVM if not args.nokvm else CPUTypes.ATOMIC,
    switch_core_type = CPUTypes.TIMING if not args.o3 else CPUTypes.O3,
    isa=ISA.X86,
    num_cores=args.cores
)

# Set up the board
board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# GAP command string (copied to disk image and executed)
if(args.benchmark in [ "bc", "bfs", "cc", "pr" ]):
    if(args.size == "small"):
        inputName = "USA-road-d.COL.gr"
    elif(args.size == "medium"):
        inputName = "USA-road-d.CAL.gr"
    else:
        inputName = "USA-road-d.CTR.gr"

    command = (
        "cd /home/gem5/gapbs;"
        + "./{} -n 1 -r 1 -f ../graphs/roads/{};".format(args.benchmark, inputName)
    )
elif(args.benchmark in [ "sssp" ]):
    if(args.size == "small"):
        inputName = "g100k.wsg"
    elif(args.size == "medium"):
        inputName = "g1m.wsg"
    else:
        inputName = "g4m.wsg"

    command = (
        "cd /home/gem5/gapbs;"
        + "./{} -n 1 -r 1 -f ../graphs/synth/{};".format(args.benchmark, inputName)
    )
elif(args.benchmark in [ "tc" ]):
    if(args.size == "small"):
        inputName = "g100k.sg"
    elif(args.size == "medium"):
        inputName = "g500k.sg"
    else:
        inputName = "g1m.sg"

    command = (
        "cd /home/gem5/gapbs;"
        + "./{} -n 1 -r 1 -f ../graphs/synth/{};".format(args.benchmark, inputName)
    )
# Parsec command string
else:
    inputName = "sim" + args.size;
    command = (
        "cd /home/gem5/parsec-benchmark;"
        + "source env.sh;"
        + "parsecmgmt -a run -p {} -c gcc-hooks -i {} -n {};".format(args.benchmark,
                                                                inputName, args.cores)
    )

# Set up workload
board.set_kernel_disk_workload(
    kernel = Resource("x86-linux-kernel-4.19.83"),
    disk_image = CustomDiskImageResource(
        "/scratch/cluster/moneil/share/cs395t/gap-and-parsec-image",
        "1"
    ),
    readfile_contents = command
)

def workbegin_handler():
    global start_tick
    print("***Switching to timing processor at start of ROI")
    processor.switch()
    print("===Entering stats ROI")
    m5.stats.reset()
    start_tick = m5.curTick()
    yield False

def workend_handler():
    print("===Exiting stats ROI")
    m5.stats.dump()
    yield True # Out of ROI, stop simulation

# Set up simulator and define what happens on exit events
simulator = Simulator(
    board = board,
    on_exit_event = {
        ExitEvent.WORKBEGIN : workbegin_handler(),
        ExitEvent.WORKEND : workend_handler(),
    }
)

start_tick = 0
globalStart = time.time()

print("***Beginning simulation!")
simulator.run()

end_tick = m5.curTick()

exit_cause = simulator.get_last_exit_event_cause()
if(exit_cause == "workend"):
    print("***Exited simulation due to ROI end")
else:
    print("***WARNING: Exited simulation due to unexpected cause: {}".format(exit_cause))
print("Simulated ticks in ROIs: %.2f" % (end_tick - start_tick))
print("Total wallclock time: %.2f s = %.2f min" % (time.time() - globalStart, (time.time() - globalStart) / 60))
