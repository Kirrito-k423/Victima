#ifndef CUCKOO_TLB_H
#define CUCKOO_TLB_H

#include "fixed_types.h"
#include "./cuckoo/elastic_cuckoo_table.h"
// #include <util.h>
#include "pagetable_walker.h"
#include "stats.h"
#include "config.hpp"
#include "simulator.h"
#include "core_manager.h"
// #include "lock.h"
// #include <vector>



namespace ParametricDramDirectoryMSI
{
    class CUCKOO_TLB
    {
    private:
        static  UInt32 SIM_PAGE_SHIFT; // 4KB
        static  IntPtr SIM_PAGE_SIZE;
        static  IntPtr SIM_PAGE_MASK;
        static  ParametricDramDirectoryMSI::MemoryManager* m_manager; // To access the memory subsystem

        core_id_t m_core_id;

        ShmemPerfModel* m_shmem_perf_model;

        elasticCuckooTable_t elasticCuckooHT_4KB;
        elasticCuckooTable_t elasticCuckooHT_2MB;
        IntPtr currentTableAddr = 0x1000000;
        IntPtr migrateTableAddr = 0x900000000;
        // UInt64 cuckoo_faults;
        // UInt64 cuckoo_hits;
        SubsecondTime cuckoo_latency;
        SubsecondTime current_lat;
        
        bool is_cuckoo_potm;
        PageTableWalker *ptw;
        UInt64 m_access, m_hits, m_miss, m_eviction;

        // For accessing the cache system
        CacheCntlr *cache; 
        IntPtr eip; 

    public:
        enum where_t
        {
            HIT = 0,
            MISS
        };
        CUCKOO_TLB(String name, String cfgname, core_id_t core_id, ShmemPerfModel* m_shmem_perf_model, int d, char* hash_func, int size, float rehash_threshold, uint8_t scale, uint8_t swaps, uint8_t priority, int* page_size_list, int page_sizes, PageTableWalker* _ptw); 

        CUCKOO_TLB::where_t lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, int level, bool model_count, Core::lock_signal_t lock, int page_size, CacheCntlr* l1dcache, ShmemPerfModel* shmem_perf_model);
        ShmemPerfModel* getShmemPerfModel() { return m_shmem_perf_model; }
        void allocate(IntPtr address, SubsecondTime now, Core::lock_signal_t locksss, int page_size, CacheCntlr* l1dcache, ShmemPerfModel* shmem_perf_model);
        void setMemManager(ParametricDramDirectoryMSI::MemoryManager* _m_manager){ CUCKOO_TLB::m_manager = _m_manager;}
        void setCUCKOOPOTMDataStructure(int d, int num_entries){ 
            std::cout << "[VM] POTM: Setting up CUCKOO POTM data structure as L3 TLB with " << d << "cuckoo tables, and each table with" << num_entries << " entries" << std::endl;
        }

    };
}


#endif // CUCKOO_TLB_H