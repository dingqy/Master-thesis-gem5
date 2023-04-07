from m5.objects import *
from .base_core import CS395T_BaseCPU

"""
Branch predictors
I've implemented one for you and aligned some default values with Champsim.
You could do the same with others.

For all branch pred options, see src/cpu/pred/BranchPredictor.py
"""

# Configured Local Branch Predictor
class CS395T_LocalBP(LocalBP):
    # predictor-specific params (from class LocalBP)
    localPredictorSize = 8192
    localCtrBits = 2

    # general BranchPredictor params
    BTBEntries = 8192
    RASSize = 64

    def __init__(self):
        super().__init__()

"""
Configure our CPU by overriding the default values for
ROB entry count, LQ entry count, SQ entry count, and branch predictor

For all CPU options, see src/cpu/o3/BaseO3CPU.py class BaseO3CPU
"""
class CS395T_CPU(CS395T_BaseCPU):
    # HINT: These override the inherited class variables, which is
    # fine if you don't plan to change a parameter value often
    numROBEntries = 224
    LQEntries = 72
    SQEntries = 56

    def set_branch_predictor(self, bp : str):
        # See full list of branch predictors in
        #   src/cpu/pred/BranchPredictor.py
        # You'd use this function to support more options
        if bp not in ["bimode"]:
            raise NotImplementedError(
                "Unsupported branch predictor"
            )

    # HINT: These overshadow the inherited class value of teh same name
    # with an instance variable. This is OK, and can be useful for setting
    # class parameters based on command-line args.
    def __init__(self, **kwargs):
        print("Creating CS395T_CPU")
        super().__init__(**kwargs);

        self.branchPred = CS395T_LocalBP()
