"""
Run a simulation in full-system mode with N cores (default=2),
on a TIMING core.  Optionally, start from a checkpoint post-kernel
boot.  Optionally, create checkpoints every X instructions in ROI,
which can be restored and simulated from for some number of warmup
and timed instructions.
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
from gem5.components.processors.simple_processor import SimpleProcessor
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
parser.add_argument("--command", type=str, help="instead of running a benchmark and size, run an arbitrary command")
parser.add_argument("--o3", default=False, action='store_true', help="use O3 core for ROI instead of Timing")
parser.add_argument("--cores", type=int, default=2, help="number of cores")
parser.add_argument("--checkpoint-roi", type=str, help="create a single checkpoint at the start of ROI in dir CHECKPOINT_ROI and then exit")
parser.add_argument("--take-checkpoints", type=int, help="create checkpoint every TAKE_CHECKPOINTS million instructions inside ROI")
parser.add_argument("--checkpoint-path", type=str, default="checkpoints", help="the directory in which to store TAKE_CHECKPOINTS checkpoints")
parser.add_argument("--restore", type=str, help="restore simulation from ROI checkpoint RESTORE")
parser.add_argument("--warmup", type=int, help="warm up for WARMUP million instructions after restoring from checkpoint")
parser.add_argument("--insts", type=int, help="simulate for INSTS million instructions after warmup, after restoring from checkpoint")
parser.add_argument("--init-checkpoint", type=str, help="create a post-kernel-boot checkpoint with atomic core and exit")
parser.add_argument("--start-from", type=str, help="start benchmark execution from a post-kernel-boot checkpoint in START_FROM")
args = parser.parse_args()

requires(
    isa_required=ISA.X86,
)

if(not ((args.benchmark and args.size) or args.command) and not (args.restore or args.init_checkpoint)):
    print("--benchmark/--size or --command are required when not restoring from checkpoint or creating init checkpoint")
    sys.exit(1)
if((args.take_checkpoints or args.checkpoint_roi or args.init_checkpoint) and args.restore):
    print("checkpoint creation and --restore are mutually exclusive!")
    sys.exit(1)
if(args.init_checkpoint and (args.take_checkpoints or args.checkpoint_roi)):
    print("--init-checkpoint and --take-checkpoints/--checkpoint-roi are mutually exclusive!")
    sys.exit(1)
if(args.init_checkpoint and args.start_from):
    print("--init-checkpoint and --start-from are mutually exclusive!")
    sys.exit(1)
if(args.insts and not args.restore):
    print("--insts flag is only valid in combination with --restore")
    sys.exit(1)
if(args.warmup and not args.restore):
    print("--warmup flag is only valid in combination with --restore")
    sys.exit(1)

if(args.warmup):
    if(args.warmup < 0):
        print("WARMUP must be non-negative")
        sys.exit(1)
    args.warmup *= 1000000
if(args.insts):
    if(args.insts < 1):
        print("INSTS must be positive")
        sys.exit(1)
    args.insts *= 1000000
if(args.take_checkpoints):
    if(args.take_checkpoints < 1):
        print("TAKE_CHECKPOINTS must be positive")
        sys.exit(1)
    args.take_checkpoints *= 1000000

# Setup the system memory
# (note: the X86 board supports only 3GB of main memory)
# memory type could be different between checkpoint and restore, but size must be fixed
memory = DualChannelDDR4_2400(size="3GB")

# Set up the cache hierarchy. This can differ between checkpoint and restore, because
# cache state is not saved in the checkpoint.
if(args.take_checkpoints or args.checkpoint_roi or args.init_checkpoint):
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

# Set up the processor. This can also differ between checkpoint and restore,
# though the core count should match.
if(args.take_checkpoints or args.checkpoint_roi or args.init_checkpoint):
    processor = SimpleProcessor(
        cpu_type = CPUTypes.ATOMIC,
        isa=ISA.X86,
        num_cores=args.cores
    )
else:
    processor = SimpleProcessor(
        cpu_type = CPUTypes.TIMING if not args.o3 else CPUTypes.O3,
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

if(args.benchmark and args.size):
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

# For initial post-kernel checkpoint, we use the hackish magic that is
# Joel Hestness's infamous hack_back_ckpt.rcS: The first time we invoke our
# runscript, we call m5 checkpoint. On restore, we use an environment variable
# to detect we should instead load a NEW runscript, which has the command
# to actually run our benchmark.  This way our post-kernel checkpoint can be used
# to invoke any arbitrary command on the disk image rather than being restricted
# to a single, particular benchmark invocation.
hackback=("""
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
if(args.init_checkpoint):
    chkptDir = Path(args.init_checkpoint)
    chkptDir.mkdir(exist_ok=True)
elif(args.checkpoint_roi):
    chkptDir = Path(args.checkpoint_roi)
    chkptDir.mkdir(exist_ok=True)
elif(args.take_checkpoints):
    chkptDir = Path(args.checkpoint_path)
    chkptDir.mkdir(exist_ok=True)
# Verify checkpoint restore directory exists
if(args.restore or args.start_from):
    if(args.restore):
        chkptDir_restore = Path(args.restore)
    else:
        chkptDir_restore = Path(args.start_from)
    if(not chkptDir_restore.exists()):
        print("Checkpoint dir {} does not exist!".format(args.restore))
        sys.exit(1)
    print("###Restoring checkpoint from: {}".format(chkptDir_restore))

if(args.init_checkpoint):
    commandStr = hackback
elif(args.restore):
    commandStr = None
elif(args.command):
    commandStr = args.command
else:
    commandStr = command

# Set up workload (including checkpoint to restore from)
board.set_kernel_disk_workload(
    kernel = Resource("x86-linux-kernel-4.19.83"),
    disk_image = CustomDiskImageResource(
        "/scratch/cluster/moneil/share/cs395t/gap-and-parsec-image",
        "1"
    ),
    checkpoint = chkptDir_restore if (args.restore or args.start_from) else None,
    readfile_contents = commandStr
)

def workbegin_handler():
    global start_tick
    print("===Entering stats ROI")
    m5.stats.reset()
    start_tick = m5.curTick()
    yield False

def workend_handler():
    print("===Exiting stats ROI")
    m5.stats.dump()
    yield True # Out of ROI, stop simulation

def checkpoint_roibegin_handler():
    global start_tick
    print("===Entering stats ROI")
    m5.stats.reset()
    start_tick = m5.curTick()
    print("###Checkpoint created at start of ROI: {}".format(chkptDir))
    simulator.save_checkpoint(chkptDir)
    yield True

def checkpoints_workbegin_handler():
    global start_tick
    print("===Entering stats ROI")
    m5.stats.reset()
    print("###Taking checkpoints every {} instructions!".format(args.take_checkpoints))
    start_tick = m5.curTick()
    simulator.schedule_max_insts(args.take_checkpoints)
    checkpoint = (chkptDir / f"chkpt.{str(start_tick)}").as_posix()
    print("###Checkpoint 1 (start of ROI): {}".format(checkpoint))
    simulator.save_checkpoint(checkpoint)
    yield False

def checkpoints_maxinsts_handler():
    checkpoint_num = 1
    while True:
        checkpoint_num += 1
        checkpoint = (chkptDir / f"chkpt.{str(m5.curTick())}").as_posix()
        print("###Checkpoint {}: {}".format(checkpoint_num, checkpoint))
        simulator.save_checkpoint(checkpoint)
        simulator.schedule_max_insts(args.take_checkpoints)
        yield False

def restore_maxinsts_handler():
    global start_tick
    if(args.warmup and args.warmup > 0):
        # first max_insts will be end of warmup
        print("===Entering stats ROI")
        m5.stats.reset()
        start_tick = m5.curTick()
        # schedule next maxinsts end if we're running limited instruction count
        if(args.insts):
            simulator.schedule_max_insts(args.insts)
        yield False
    # second max_insts will be end of ROI
    print("===Exiting stats ROI")
    m5.stats.dump()
    yield True # stop simulation

def checkpoint_handler():
    print("###Taking post-kernel-boot checkpoint")
    simulator.save_checkpoint(chkptDir)
    yield True

if(args.take_checkpoints):
    event_handlers = {
        ExitEvent.WORKBEGIN : checkpoints_workbegin_handler(),
        ExitEvent.WORKEND : workend_handler(),
        ExitEvent.MAX_INSTS : checkpoints_maxinsts_handler()
    }
elif(args.checkpoint_roi):
    event_handlers = {
        ExitEvent.WORKBEGIN : checkpoint_roibegin_handler()
    }
elif(args.restore):
    event_handlers = {
        ExitEvent.WORKEND : workend_handler(),
        ExitEvent.MAX_INSTS : restore_maxinsts_handler()
    }
elif(args.init_checkpoint):
    event_handlers = {
        ExitEvent.CHECKPOINT : checkpoint_handler()
    }
else:
    event_handlers = {
        ExitEvent.WORKBEGIN : workbegin_handler(),
        ExitEvent.WORKEND : workend_handler(),
    }

# Set up simulator and define what happens on exit events
simulator = Simulator(
    board = board,
    on_exit_event = event_handlers
)

start_tick = 0
globalStart = time.time()
print("***Beginning simulation!")

if(args.restore):
    if(args.warmup and args.warmup > 0):
        simulator.schedule_max_insts(args.warmup)
    else:
        print("===Entering stats ROI")
        m5.stats.reset()
        if(args.insts):
            simulator.schedule_max_insts(args.insts)

simulator.run()
end_tick = m5.curTick()

exit_cause = simulator.get_last_exit_event_cause()
if(exit_cause == "workend"):
    print("***Exited simulation due to ROI end")
elif(exit_cause == "a thread reached the max instruction count"):
    print("***Exited simulation due to INSTS reached");
elif(exit_cause == "checkpoint"):
    print("***Exited simulation after checkpoint at kernel boot")
elif(exit_cause == "workbegin"):
    print("***Exited simulation after checkpoint at ROI start")
else:
    print("***WARNING: Exited simulation due to unexpected cause: {}".format(exit_cause))
print("Simulated ticks in ROIs: %.2f" % (end_tick - start_tick))
print("Total wallclock time: %.2f s = %.2f min" % (time.time() - globalStart, (time.time() - globalStart) / 60))
