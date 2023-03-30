#ifndef __MEM_CACHE_CACHE_PARTITION_HH__
#define __MEM_CACHE_CACHE_PARTITION_HH__

#include <unordered_map>
#include <cstdint>
#include "sim/system.hh"
#include "mem/cache/replacement_policies/base.hh"

namespace gem5 
{

class Flock 
{
  private:
    std::unordered_map<int, int> parition_budget;

    uint64_t counter;

    uint64_t max_reset_budget;

    System *system;

    int _num_ways;

    int _num_cpus;

  public:

    int updateBudget();

    int getCurrFCP();

    int getProjFCP(int partition, int context_id, replacement_policy::Base *replacement_policy);

}


}

#endif //__MEM_CACHE_CACHE_PARTITION_HH__