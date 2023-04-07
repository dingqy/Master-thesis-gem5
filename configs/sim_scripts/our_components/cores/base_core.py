from m5.objects import *

# Modeled loosely on an Intel Skylake.
# Sources:
# - https://github.com/darchr/gem5-skylake-config
# - https://www.csl.cornell.edu/courses/ece4750/handouts/ece4750-section-skylake.pdf

"""
Set up the functional units within the CPU
"""
class port0(FUDesc):
    opList = [ OpDesc(opClass='IntAlu', opLat=1),
               OpDesc(opClass='IntDiv', opLat=1, pipelined=True),
               OpDesc(opClass='FloatDiv', opLat=12, pipelined=True),
               OpDesc(opClass='FloatSqrt', opLat=24, pipelined=True),
               OpDesc(opClass='FloatAdd', opLat=4, pipelined=True),
               OpDesc(opClass='FloatCmp', opLat=4, pipelined=True),
               OpDesc(opClass='FloatCvt', opLat=4, pipelined=True),
               OpDesc(opClass='FloatMult', opLat=4, pipelined=True),
               OpDesc(opClass='FloatMultAcc', opLat=5, pipelined=True),
               OpDesc(opClass='SimdAdd', opLat=1),
               OpDesc(opClass='SimdAddAcc', opLat=1),
               OpDesc(opClass='SimdAlu', opLat=1),
               OpDesc(opClass='SimdCmp', opLat=1),
               OpDesc(opClass='SimdShift', opLat=1),
               OpDesc(opClass='SimdShiftAcc', opLat=1),
               OpDesc(opClass='SimdReduceAdd', opLat=1),
               OpDesc(opClass='SimdReduceAlu', opLat=1),
               OpDesc(opClass='SimdReduceCmp', opLat=1),
               OpDesc(opClass='SimdCvt', opLat=3, pipelined=True),
               OpDesc(opClass='SimdMisc'),
               OpDesc(opClass='SimdMult', opLat=4, pipelined=True),
               OpDesc(opClass='SimdMultAcc', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatAlu', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatCmp', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceCmp', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatCvt', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatMult', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatMultAcc', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatDiv', opLat=12, pipelined=True),
               OpDesc(opClass='SimdFloatSqrt', opLat=20, pipelined=True)
               ]
    count = 1

class port1(FUDesc):
    opList = [ OpDesc(opClass='IntAlu', opLat=1),
               OpDesc(opClass='IntMult', opLat=3, pipelined=True),
               OpDesc(opClass='FloatAdd', opLat=4, pipelined=True),
               OpDesc(opClass='FloatCmp', opLat=4, pipelined=True),
               OpDesc(opClass='FloatCvt', opLat=4, pipelined=True),
               OpDesc(opClass='FloatMult', opLat=4, pipelined=True),
               OpDesc(opClass='FloatMultAcc', opLat=5, pipelined=True),
               OpDesc(opClass='SimdAdd', opLat=1),
               OpDesc(opClass='SimdAddAcc', opLat=1),
               OpDesc(opClass='SimdAlu', opLat=1),
               OpDesc(opClass='SimdCmp', opLat=1),
               OpDesc(opClass='SimdShift', opLat=1),
               OpDesc(opClass='SimdShiftAcc', opLat=1),
               OpDesc(opClass='SimdReduceAdd', opLat=1),
               OpDesc(opClass='SimdReduceAlu', opLat=1),
               OpDesc(opClass='SimdReduceCmp', opLat=1),
               OpDesc(opClass='SimdCvt', opLat=3, pipelined=True),
               OpDesc(opClass='SimdMisc'),
               OpDesc(opClass='SimdMult', opLat=4, pipelined=True),
               OpDesc(opClass='SimdMultAcc', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatAlu', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatCmp', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceCmp', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatCvt', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatMult', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatMultAcc', opLat=4, pipelined=True)
               ]
    count = 1

class port2(FUDesc):
    opList = [ OpDesc(opClass='MemRead', opLat=1, pipelined=True),
               OpDesc(opClass='FloatMemRead', opLat=1, pipelined=True)
               ]
    count = 1

class port3(FUDesc):
    opList = [ OpDesc(opClass='MemRead', opLat=1, pipelined=True),
               OpDesc(opClass='FloatMemRead', opLat=1, pipelined=True)
               ]
    count = 1

class port4(FUDesc):
    opList = [ OpDesc(opClass='MemWrite', opLat=1, pipelined=True),
               OpDesc(opClass='FloatMemWrite', opLat=1, pipelined=True)
               ]
    count = 1

class port5(FUDesc):
    opList = [ OpDesc(opClass='IntAlu', opLat=1),
               OpDesc(opClass='SimdAdd', opLat=1),
               OpDesc(opClass='SimdAddAcc', opLat=1),
               OpDesc(opClass='SimdAlu', opLat=1),
               OpDesc(opClass='SimdCmp', opLat=1),
               OpDesc(opClass='SimdShift', opLat=1),
               OpDesc(opClass='SimdMisc'),
               OpDesc(opClass='SimdShiftAcc', opLat=1),
               OpDesc(opClass='SimdReduceAdd', opLat=1),
               OpDesc(opClass='SimdReduceAlu', opLat=1),
               OpDesc(opClass='SimdReduceCmp', opLat=1),
               OpDesc(opClass='SimdFloatAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatAlu', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatCmp', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceAdd', opLat=4, pipelined=True),
               OpDesc(opClass='SimdFloatReduceCmp', opLat=4, pipelined=True)
               ]
    count = 1

class port6(FUDesc):
     opList = [ OpDesc(opClass='IntAlu', opLat=1),
                OpDesc(opClass='SimdAdd', opLat=1),
                OpDesc(opClass='SimdAddAcc', opLat=1),
                OpDesc(opClass='SimdAlu', opLat=1),
                OpDesc(opClass='SimdCmp', opLat=1),
                OpDesc(opClass='SimdShift', opLat=1),
                OpDesc(opClass='SimdShiftAcc', opLat=1)
                ]
     count = 1

class port7(FUDesc):
     opList = [ OpDesc(opClass='MemWrite', opLat=1, pipelined=True),
                OpDesc(opClass='FloatMemWrite', opLat=1, pipelined=True)
                ]
     count = 1

class ExecUnits(FUPool):
    FUList = [ port0(), port1(), port2(), port3(), port4(), port5(), port6(), port7() ]

"""
Configure our TLBs
"""
class CS395T_MMU(X86MMU):
    itb = X86TLB(entry_type="instruction", size=64)
    dtb = X86TLB(entry_type="data", size=64)

"""
Create an out-of-order CPU with some default options
and the functional units specified above
"""
# For all CPU options, see src/cpu/o3/BaseO3CPU.py class BaseO3CPU
# (O3CPU will be defined to be X86O3CPU for X86 simulations)
depth = 3
width = 4
class CS395T_BaseCPU(O3CPU):
    # Pipeline delays
    # Scale default 5-stage pipeline by depth
    # Here, we're modeling a 15-stage pipeline
    fetchToDecodeDelay = depth
    decodeToRenameDelay = 1
    renameToIEWDelay = 3*depth
    iewToCommitDelay = 2* depth

    forwardComSize = 19
    backComSize = 19

    fuPool = ExecUnits()

    # Pipeline widths
    fetchWidth = width
    decodeWidth = width
    renameWidth = 2*width
    dispatchWidth = 2*width
    issueWidth = 2*width
    wbWidth = 2*width
    commitWidth = 2*width
    squashWidth = 2*width

    fetchBufferSize = 16
    fetchQueueSize = 50
    numROBEntries = 224
    numIQEntries = 97
    numPhysIntRegs = 180
    numPhysFloatRegs = 168

    # Default branch predictor
    branchPred = NULL

    # TLBs
    mmu = CS395T_MMU()

    def __init__(self, **kwargs):
        super().__init__(**kwargs)
