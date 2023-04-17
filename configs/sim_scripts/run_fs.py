"""
Run a simulation in full-system mode,
beginning execution on the KVM (or atomic) core and, beginning
at benchmark execution, switching between KVM and O3 at a periodic
sampling rate with an ROI stats section for each switch.
"""
import os
import sys
import time
import argparse
import m5
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

parser = argparse.ArgumentParser(
    description="Invoke a binary in FS mode, fast-forwarding to binary start and then optionally sampling (iteratively fast-forwards for ff_interval instructions, then switches to timing core and runs for warmup_interval instructions before collecting stats for roi_interval instructions)"
)
parser.add_argument("--benchmark", help="benchmark to run")
parser.add_argument("--nokvm", default=False, action='store_true', help="use atomic core for fast-forwarding")
parser.add_argument("--init_ff", type=int, help="fast-forward the first INIT_FF million instructions after benchmark start")
parser.add_argument("--sample", default=False, action='store_true', help="run with periodic sampling")
parser.add_argument("ff_interval", nargs='?', type=int, help="fast-forwarding interval, # of instructions in millions (required with --sample)")
parser.add_argument("warmup_interval", nargs='?', type=int, help="warmup interval, # of instructions in millions (required with --sample)")
parser.add_argument("roi_interval", nargs='?', type=int, help="ROI interval, # of instructions in millions (required with --sample)")
parser.add_argument("--max_rois", type=int, help="in sample mode, stop sampling after MAX_ROIS ROIs (default: no max)")
parser.add_argument("--continue", default=False, action='store_true', dest="continueSim", help="in sample mode, after MAX_ROIS ROIs, continue fast-forward execution (default: terminate)")
parser.add_argument("--l2repl", default="lru")
parser.add_argument("--l3repl", default="lru")
parser.add_argument("--num_cores", type=int, default=1)
args = parser.parse_args()

if(args.sample):
    if(args.ff_interval is None or args.warmup_interval is None or args.roi_interval is None):
        print("Sample mode requires three positional args: ff_interval warmup_interval roi_interval")
        sys.exit(1)
    if(args.max_rois):
        if(args.max_rois < 1):
            print("MAX_ROIS must be positive")
            sys.exit(1)
    else:
        if(args.continueSim):
            print("--continue is only valid with --max_rois")
            sys.exit(1)
    args.ff_interval *= 1000000
    args.warmup_interval *= 1000000
    args.roi_interval *= 1000000
else:
    if(args.ff_interval):
        print("A sample interval was specified but --sample omitted")
        sys.exit(1)
    if(args.max_rois or args.continueSim):
        print("Flags --max_rois and --continue are invalid without --sample mode")
        sys.exit(1)
if(args.init_ff):
    if(args.init_ff < 1):
        print("INIT_FF must be positive")
        sys.exit(1)
    args.init_ff *= 1000000

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
   l2_repl = args.l2repl,
   llc_pref = "none",
   llc_repl = args.l3repl
)

# This is hackish, but the SimpleProcessor models only understand the
# built-in CPU types, and we've created our own. Overload the method
# that AbstractCore uses to create new cores to make it use our own
# when "O3" is specified
SimpleCore.cpu_simobject_factory = CS395T_SimpleCore.cs395t_cpu_factory

# Set up the processor (which contains some number of cores)
# A switchable processor will start simulation in the first processor
# and switch to the second on a call to processor.switch()
# In stats.txt, stats will be separated between processor.start.*
# and processor.switch.*
processor = SimpleSwitchableProcessor(
    starting_core_type = CPUTypes.KVM if not args.nokvm else CPUTypes.ATOMIC,
    switch_core_type = CPUTypes.O3,
    isa=ISA.X86,
    num_cores=args.num_cores
)

# Set up the board.
board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Run command is copied into the disk image via readfile_contents and executed.
# Anything you want to run on a timing core should be wrapped in m5 workbegin/end.
# Your command should end in m5 exit; otherwise, simulation will drop to a shell
# prompt at the end rather than exiting Gem5.
commands = {
        'astar':      'cd spec06/473.astar      ; ./473.astar BigLakes2048.cfg',
        'bwaves':     'cd spec06/410.bwaves     ; ./410.bwaves',
        'bzip':       'cd spec06/401.bzip2      ; ./401.bzip2 liberty.jpg 30',
        'cactusADM':  'cd spec06/436.cactusADM  ; ./436.cactusADM benchADM.par',
        'calculix':   'cd spec06/454.calculix   ; ./454.calculix -i hyperviscoplastic',
        'gcc':        'cd spec06/403.gcc        ; ./403.gcc 166.i -o 166.s',
        'GemsFDTD':   'cd spec06/459.GemsFDTD   ; ./459.GemsFDTD',
        'h264ref':    'cd spec06/464.h264ref    ; ./464.h264ref -d foreman_ref_encoder_baseline.cfg',
        'hmmer':      'cd spec06/456.hmmer      ; ./456.hmmer nph3.hmm swiss41',
        'lbm':        'cd spec06/470.lbm        ; ./470.lbm 3000 reference.dat 0 0 100_100_130_ldc.of',
        'leslie':     'cd spec06/437.leslie3d   ; ./437.leslie3d < leslie3d.in',
        'libquantum': 'cd spec06/462.libquantum ; ./462.libquantum 1397 8',
        'mcf':        'cd spec06/429.mcf        ; ./429.mcf inp.in',
        'milc':       'cd spec06/433.milc       ; ./433.milc < su3imp.in',
        'omnetpp':    'cd spec06/471.omnetpp    ; ./471.omnetpp omnetpp.ini',
        'soplex':     'cd spec06/450.soplex     ; ./450.soplex -s1 -e -m45000 pds-50.mps',
        'sphinx3':    'cd spec06/482.sphinx3    ; ./482.sphinx3 ctlfile . args.an4',
        'tonto':      'cd spec06/465.tonto      ; ./465.tonto',
        'xalancbmk':  'cd spec06/483.xalancbmk  ; ./483.xalancbmk -v t5.xml xalanc.xsl > /dev/null',
        'zeusmp':     'cd spec06/434.zeusmp     ; ./434.zeusmp',
        'bfs':        'cd gap                   ; ./bfs -r 1 -f ./graphs/g22.el',
        'cc':         'cd gap                   ; ./cc -r 1 -f ./graphs/g22.el',
        'pr':         'cd gap                   ; ./pr -r 1 -f ./graphs/g22.el',
        'sssp':       'cd gap                   ; ./sssp -r 1 -f ./graphs/g22.el',
        'tc':         'cd gap                   ; ./tc -r 1 -f ./graphs/g22.el',
        "bzip_gcc":     "spec06/401.bzip2/401.bzip2 spec06/401.bzip2/liberty.jpg 30 & spec06/403.gcc/403.gcc spec06/403.gcc/166.i -o spec06/403.gcc/166.s"
}
command = (
    "m5 workbegin;"
    + commands[args.benchmark] + ';'
    + "m5 workend;"
    + "sleep 5;" # don't cut off output
    + "m5 exit;"
)

# If you leave the command str blank, the simulation will drop to a shell prompt
# and you can run any command you want, including executing benchmarks wrapped in
# workbegin/end, e.g.,:
#  "m5 workbegin; ./matmul_small; m5 workend;"
# (Caution: this doesn't always play nice with --sample mode.)
# To interact with the simulated shell prompt, build m5term and connect:
#    > cd gem5/util/term; make
#    > ./gem5/util/term/m5term localhost 3456

# Resource() attempts to download the kernel from gem5-resources if not 
# already present in $GEM5_RESOURCES_DIR.
board.set_kernel_disk_workload(
    kernel = Resource("x86-linux-kernel-4.19.83"),
    # The second argument here tells Gem5 where the root partition is
    # This string will be appended to /dev/hda and used in the kernel command
    disk_image = CustomDiskImageResource(
        "/home/dingqy/boot-tests/gem5-resources/src/x86-ubuntu/gem5/configs/sim_scripts/benchmarks_image",
        "1"
    ),
    readfile_contents=command
)

class Interval:
    NO_WORK = 1 # not even in benchmark
    FF_INIT = 2 # in initial FF window (--init_ff)
    FF_WORK = 3 # sampling FF interval
    WARMUP = 4  # sampling warmup interval
    ROI = 5     # sampling ROI interval

def workbegin_handler():
    global current_interval
    global completed_rois
    global start_tick
    global start_time
    # Start of benchmark execution.
    while True:
        completed_rois = 0
        print("***Beginning benchmark execution")
        # Initial fast-forward set: no core switch, but set up next exit event
        if(args.init_ff):
            print("***Beginning initial fast-forward")
            current_interval = Interval.FF_INIT
            simulator.schedule_max_insts(args.init_ff)
        # Sampling mode: Still fast-forwarding (no core switch), but set up
        # exit event for the end of our fast-forwarding window
        elif(args.sample):
            current_interval = Interval.FF_WORK
            simulator.schedule_max_insts(args.ff_interval)
        # Whole-benchmark mode: Switch cores to timing and start ROI
        else:
            current_interval = Interval.ROI
            print("***Switching to timing processor at benchmark start")
            processor.switch()
            print("===Entering stats ROI #1 at benchmark start")
            m5.stats.reset()
            start_tick = m5.curTick()
        start_time = time.time()
        yield False # continue .run()

def workend_handler():
    global current_interval
    global completed_rois
    global total_ticks
    global start_tick
    global start_time
    # Benchmark is over; dump final stats block if we're mid-ROI
    # and make sure we're back in the FF processor
    while True:
        print("***End of benchmark execution")
        if(current_interval == Interval.ROI):
            print("===Exiting stats ROI #{} at benchmark end. Took {} seconds".format(completed_rois + 1, time.time() - start_time))
            m5.stats.dump()
            end_tick = m5.curTick()
            total_ticks += (end_tick - start_tick)
            completed_rois += 1
        if(current_interval in [Interval.ROI, Interval.WARMUP]):
            print("***Switching to fast-forward processor for post-benchmark")
            processor.switch()
        # Gem5 will always dump a final stats block when it exits, if any stats
        # have changed since the last one.  This means that we'll end up with an
        # annoying unwanted final stats block of non-ROI. At least zero it out.
        m5.stats.reset()
        current_interval = Interval.NO_WORK
        yield False # continue .run(), we haven't seen an m5 exit yet

def maxinsts_handler():
    global current_interval
    global completed_rois
    global total_ticks
    global start_tick
    global start_time
    while True:
        # ROI --> FF_WORK: end of ROI, dump stats and switch to FF processor
        if(current_interval == Interval.ROI):
            print("===Exiting stats ROI #{}. Took {} seconds".format(completed_rois + 1, time.time() - start_time))
            m5.stats.dump()
            end_tick = m5.curTick()
            total_ticks += (end_tick - start_tick)
            completed_rois += 1
            print("***Switching to fast-forward processor")
            processor.switch()
            current_interval = Interval.FF_WORK
            # Schedule end of FF_WORK interval (if we're not out of max ROIs)
            if(args.max_rois and completed_rois >= args.max_rois):
                if(args.continueSim):
                    print("***Max ROIs reached, fast-forwarding remainder of benchmark")
                else:
                    print("***Max ROIs reached, terminating simulation")
                    m5.stats.reset() # reset stats first, to handle unwanted final block
                    yield True # terminate .run()
            else:
                simulator.schedule_max_insts(args.ff_interval)
        # WARMUP --> ROI: end of warmup, reset stats and start ROI (proc already timing)
        elif(current_interval == Interval.WARMUP):
            print("===Entering stats ROI #{}. Warmup took {} seconds".format(completed_rois + 1, time.time() - start_time))
            m5.stats.reset()
            start_tick = m5.curTick()
            current_interval = Interval.ROI
            # Schedule end of ROI interval
            simulator.schedule_max_insts(args.roi_interval)
        # FF_WORK --> WARMUP: end of fast-forward, switch to timing proc for warmup
        elif(current_interval == Interval.FF_WORK):
            print("***Switching to timing processor. Fast forward took {} seconds".format(time.time() - start_time))
            processor.switch()
            current_interval = Interval.WARMUP
            # Schedule end of WARMUP interval
            simulator.schedule_max_insts(args.warmup_interval)
        # FF_INIT --> FF_WORK: init_ff set, done with init, begin ff-warmup-ROI iteration
        # if we're in sample mode or ROI if we're in whole-benchmark mode
        elif(current_interval == Interval.FF_INIT):
            print("***End of initial fast-forward. Took {} seconds".format(time.time() - start_time))
            if(args.sample):
                current_interval = Interval.FF_WORK
                simulator.schedule_max_insts(args.ff_interval)
            else:
                current_interval = Interval.ROI
                print("***Switching to timing processor")
                processor.switch()
                print("===Entering stats ROI #1")
                m5.stats.reset()
                start_tick = m5.curTick()
        start_time = time.time()
        # else: (current_interval == Interval.NO_WORK): nothing to do!
        # This occurs if workend occurred mid-sample-interval, leaving one max_insts event scheduled.
        yield False # continue .run()

# Set up simulator and define what happens on exit events
# using a custom generator
simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.WORKBEGIN : workbegin_handler(),
        ExitEvent.WORKEND : workend_handler(),
        ExitEvent.MAX_INSTS : maxinsts_handler()
    }
)

total_ticks = 0
globalStart = time.time()

current_interval = Interval.NO_WORK # we start in fast-forward core, outside of benchmark
print("***Beginning simulation!")
simulator.run()

exit_cause = simulator.get_last_exit_event_cause()
if(exit_cause == "m5_exit instruction encountered"):
    print("***Exited simulation due to m5 exit")
elif(args.max_rois and not args.continueSim and exit_cause == "a thread reached the max instruction count"):
    print("***Exited simulation due to max_rois met")
else:
    print("***WARNING: Exited simulation due to unexpected cause: {}".format(exit_cause))
print("Simulated ticks in ROIs: %.2f" % total_ticks)
print("Total wallclock time: %.2f s = %.2f min" % (time.time() - globalStart, (time.time() - globalStart) / 60))
