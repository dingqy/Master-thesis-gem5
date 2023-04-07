from m5.objects import *

"""
A cache whose prefetcher and replacement policy can be configured,
  with a default of no-prefetcher and LRU replacement
  For all prefetcher options, see:
      src/mem/cache/prefetch/Prefetcher.py
  For all replacement policy options, see:
      src/mem/cache/replacement_policies/ReplacementPolicies.py
"""
class CS395T_ConfigurableCache(Cache):
    # HINT: If you want to change the prefetcher and/or replacement 
    # policy based on command-line options, this is the place to do it.
    # You could instantiate any available prefetcher Prefetcher.py, or you
    # can extend those classes to change the prefetcher's default params
    # and then instantiate that instead.
    # (See CS395T_CPU's use of LocalBP for an example.)
    # The comment at the top of this file has a path to the class definitions
    # of all the built-in prefetcher options, where you can look for a basic 
    # stride prefetcher.
    def __init__(self, pref: str, repl : str):
        super().__init__()

        if pref not in ["stride", "none"]:
            raise NotImplementedError(
                "Unsupported prefetcher"
            )
        if(pref == "stride"):
            self.prefetcher = StridePrefetcher(degree = 3)
        else:
            self.prefetcher = NULL
        
        if repl == "hawkeye":
            print("Hawkeye Enable")
            self.replacement_policy = HawkeyeRP(cache_level = 2, num_cache_sets = 1024)
        else:
            self.replacement_policy = LRURP()

        self.prefetch_on_access = True
