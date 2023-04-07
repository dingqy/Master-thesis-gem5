"""
Run a simulation in full-system mode with N cores (default=2),
beginning execution on KVM (or atomic) cores and switching
to TIMING (or O3) beginning at the ROI workbegin in the benchmark.
If flag --checkpoint is given, will create a checkpoint after
kernel boot. If flag --restore <checkpoint> is given, will restore
from a checkpoint prior to executing the given benchmark command.
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
from gem5.components.cachehierarchies.classic.no_cache import NoCache
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
parser.add_argument("--benchmark", type=str, help="benchmark to run", choices=benchmark_choices)
parser.add_argument("--size", type=str, help="input size (small, medium, large)", choices=size_choices)
parser.add_argument("--cores", type=int, default=2, help="number of cores")
parser.add_argument("--checkpoint", type=str, help="take a checkpoint after kernel boot and save in CHECKPOINT dir")
parser.add_argument("--restore", type=str, help="restore from checkpoint in CHECKPOINT dir")
parser.add_argument("--nokvm", default=False, action='store_true', help="use atomic core for fast-forwarding instead of KVM")
parser.add_argument("--o3", default=False, action='store_true', help="use O3 core for ROI instead of Timing")
args = parser.parse_args()

requires(
    isa_required=ISA.X86,
)

if(not args.benchmark and not args.checkpoint):
    print("--benchmark is required when not taking checkpoint")
    sys.exit(1)
if(not args.size and not args.checkpoint):
    print("--size is required when not taking checkpoint")
    sys.exit(1)
if(args.checkpoint and args.restore):
    print("--checkpoint and --restore are mutually exclusive!")
    sys.exit(1)

# Setup the system memory
# (note: the X86 board supports only 3GB of main memory)
# Memory type can differ between checkpoint and restore, but size must be the same
memory = DualChannelDDR4_2400(size="3GB")

# Set up the cache hierarchy. This can differ between checkpoint and restore, because
# cache state is not saved in the checkpoint.
if(args.checkpoint):
    cache_hierarchy = NoCache()
else:
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

# Set up the processor. This can differ between checkpoint and restore,
# but using the SimpleSwitchableProcessor from stdlib complicates this,
# because you'll still have the start and switch core SimObjects on restore.
# Core count must match.
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

if(not args.checkpoint):
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

# Now we really work some hackish magic, based on Joel Hestness's infamous hack_back_ckpt.rcS.
# The first time we invoke our runscript, we'll call m5 checkpoint.
# On restore, we'll use an environment variable to detect we should instead get a NEW runscript,
# which will have the command to invoke our chosen benchmark.
# This way our post-kernel checkpoint can be used to invoke any arbitrary command on this disk
# image, not restricted to a single benchmark execution.
hackback_command = ("""
    # Test if the RUNSCRIPT_VAR environment variable is already set
    if [ "${RUNSCRIPT_VAR+set}" != set ]
    then
        # Signal our future self that it's safe to continue
        export RUNSCRIPT_VAR=1
    else
        # We've already executed once, so we should exit
        /sbin/m5 exit
    fi
    # Checkpoint the first execution
    echo "Checkpointing simulation..."
    m5 checkpoint
 
    # Test if we previously okayed ourselves to run this script
    if [ "$RUNSCRIPT_VAR" -eq 1 ]
    then
        # Signal our future self not to recurse infinitely
        export RUNSCRIPT_VAR=2
        # Read the new command for the checkpoint restored execution
        echo "Loading new run command..."
        m5 readfile > new_commands.sh 
        chmod +x new_commands.sh
        # Execute the new runscript
        if [ -s new_commands.sh ]
        then
            echo "Executing new run command..."
            ./new_commands.sh
        else
            echo "ERROR: new run command not specified!"
        fi
    fi
""")

# Create checkpoint directory
if(args.checkpoint):
    chkptDir = Path(args.checkpoint)
    chkptDir.mkdir(exist_ok=True)
# Verify checkpoint restore directory
if(args.restore):
    chkptDir = Path(args.restore)
    if(not chkptDir.exists()):
        print("Checkpoint dir {} does not exist!".format(args.restore))
        sys.exit(1)
    print("###Restoring checkpoint from: {}".format(chkptDir))

# Set up workload
board.set_kernel_disk_workload(
    kernel = Resource("x86-linux-kernel-4.19.83"),
    disk_image = CustomDiskImageResource(
        "/scratch/cluster/moneil/share/cs395t/gap-and-parsec-image",
        "1"
    ),
    checkpoint = chkptDir if args.restore else None,
    readfile_contents = command if not args.checkpoint else hackback_command
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

def checkpoint_handler():
    print("###Taking simulation checkpoint")
    simulator.save_checkpoint(chkptDir)
    yield True
    
# Set up simulator and define what happens on exit events
simulator = Simulator(
    board = board,
    on_exit_event = {
        ExitEvent.WORKBEGIN : workbegin_handler(),
        ExitEvent.WORKEND : workend_handler(),
        ExitEvent.CHECKPOINT : checkpoint_handler()
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
elif(exit_cause == "checkpoint"):
    print("***Exited simulation after checkpoint")
else:
    print("***WARNING: Exited simulation due to unexpected cause: {}".format(exit_cause))
print("Simulated ticks in ROIs: %.2f" % (end_tick - start_tick))
print("Total wallclock time: %.2f s = %.2f min" % (time.time() - globalStart, (time.time() - globalStart) / 60))
