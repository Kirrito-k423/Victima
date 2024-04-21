#include "cuckoo_tlb.h"
#include <cmath>
#include <iostream>
#include <utility>
#include "memory_manager.h"


namespace ParametricDramDirectoryMSI
{
    UInt32 CUCKOO_TLB::SIM_PAGE_SHIFT;
    IntPtr CUCKOO_TLB::SIM_PAGE_SIZE;
    IntPtr CUCKOO_TLB::SIM_PAGE_MASK;

    ParametricDramDirectoryMSI::MemoryManager *CUCKOO_TLB::m_manager = NULL;

    CUCKOO_TLB::CUCKOO_TLB(String name, String cfgname, core_id_t core_id, ShmemPerfModel* _m_shmem_perf_model, 
        // Cuckoo hash related
        int d, char* hash_func, int size, float rehash_threshold, 
        uint8_t scale, // Control the scale of each rehash 
        uint8_t swaps, // Number of elements to be rehashed
        uint8_t priority, // Bias a hashtable during insertion
        // Cuckoo hash related end
        int* page_size_list, int page_sizes,
        String tlb_placement,
        PageTableWalker* _ptw)
        : m_core_id(core_id)
        , m_shmem_perf_model(_m_shmem_perf_model)
        {
            cuckoo_latency = SubsecondTime::Zero();
            m_hits = m_miss = m_access = m_eviction = 0;
            if (tlb_placement == "across_channels") {
                m_tlb_placement = tlb_placement_t::ACROSS_CAHNNELS;
            }
            else if (tlb_placement == "across_banks") {
                m_tlb_placement = tlb_placement_t::ACROSS_BANKS;
            }
            else if (tlb_placement == "normal") {
                m_tlb_placement = tlb_placement_t::NORMAL;
            }


            m_mem_info.dram_page_size = Sim()->getCfg()->getInt("perf_model/dram/ddr/dram_page_size");
            m_mem_info.channel_offset = Sim()->getCfg()->getInt("perf_model/dram/ddr/channel_offset");
            m_mem_info.num_bank = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_banks");
            m_mem_info.num_channel = Sim()->getCfg()->getInt("perf_model/dram/ddr/num_channels");
            m_mem_info.tlb_placement = m_tlb_placement;

            std::cout << "Instantiating Cuckoo Software-managed TLB " << tlb_placement << std::endl;
            create_elastic(d, size, &elasticCuckooHT_4KB, hash_func, rehash_threshold, scale, swaps, priority); //4KB cuckoo
            create_elastic(d, size, &elasticCuckooHT_2MB, hash_func, rehash_threshold, scale, swaps, priority); 
        
            registerStatsMetric(name, core_id, "hits", &m_hits);
            registerStatsMetric(name, core_id, "miss", &m_miss);
            registerStatsMetric(name, core_id, "latency", &cuckoo_latency);
            registerStatsMetric(name, core_id, "access", &m_access);
            registerStatsMetric(name, core_id, "eviction", &m_eviction);
            is_cuckoo_potm = true;

            ptw = _ptw; // Initialize page table walker

        }

    void CUCKOO_TLB::allocate(IntPtr address, SubsecondTime now, Core::lock_signal_t lock_signal, int page_size, CacheCntlr* l1dcache, ShmemPerfModel* shmem_perf_model) { // allocate POM TLB entry in cuckoo tables
        // int page_size = ptw->init_walk_functional(address);
        IntPtr vpn = address >> page_size;
        m_eviction++;

        elem_t elem_4KB;
        elem_4KB.valid = 1;
        elem_4KB.value = address >> 15; // We can assume 8 PTE entries/hash set

        elem_t elem_2MB;
        elem_2MB.valid = 1;
        elem_2MB.value = address >> 24; // We can assume 8 PTE entries/hash set


        SubsecondTime maxLat = SubsecondTime::Zero();
        SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
        std::vector<elem_t> accessedAddresses_4KB;
        std::vector<elem_t> accessedAddresses_2MB;

        if(page_size == 12) {
            insert_elastic_cuckoo_potm(&elem_2MB, &elasticCuckooHT_2MB, 0, 0, accessedAddresses_4KB, m_mem_info);
            // evaluate_elasticity(&elasticCuckooHT_2MB, 0);
        }
        else {
            insert_elastic_cuckoo_potm(&elem_4KB, &elasticCuckooHT_4KB, 0, 0, accessedAddresses_2MB, m_mem_info);
            // evaluate_elasticity(&elasticCuckooHT_4KB, 0);
        }
        if(page_size == 12) {         
            for(elem_t addr: accessedAddresses_4KB) {  
                IntPtr cache_address = addr.value & (~((64 - 1))); 		
                CacheBlockInfo::block_type_t block_type = CacheBlockInfo::block_type_t::TLB_ENTRY;
                l1dcache->processMemOpFromCore(
                    0,
                    lock_signal,
                    Core::mem_op_t::READ,
                    cache_address, 0,
                    NULL, 64,
                    true,
                    true, block_type, SubsecondTime::Zero());
                SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
                shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
                if((t_end - t_start) > maxLat) 
                    maxLat = t_end - t_start;
            }            
        }
        if(page_size == 21) {
            for(elem_t addr: accessedAddresses_2MB) {
                IntPtr cache_address = addr.value & (~((64 - 1))); 		
                CacheBlockInfo::block_type_t block_type =  CacheBlockInfo::block_type_t::TLB_ENTRY;
                l1dcache->processMemOpFromCore(
                    0,
                    lock_signal,
                    Core::mem_op_t::READ,
                    cache_address, 0,
                    NULL, 64,
                    true,
                    true, block_type, SubsecondTime::Zero());
                SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
                shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
                if((t_end - t_start) > maxLat) 
                    maxLat = t_end - t_start;
            }
        }
        cuckoo_latency += maxLat;
    }

    CUCKOO_TLB::where_t CUCKOO_TLB::lookup(IntPtr address, SubsecondTime now, bool allocate_on_miss, int level, bool model_count, Core::lock_signal_t lock_signal, int page_size, CacheCntlr* l1dcache, ShmemPerfModel* shmem_perf_model) {
        IntPtr vpn = address >> page_size;
        if(model_count) m_access++;
        
        elem_t elem_4KB;
        elem_4KB.valid = 1;
        elem_4KB.value = address >> 15; // We can assume 8 PTE entries/hash set
        std::vector<elem_t> accessedAddresses_4KB;
        if (page_size == 12)
            accessedAddresses_4KB = find_elastic_cuckoo_potm(&elem_4KB, &elasticCuckooHT_4KB, m_mem_info);

        elem_t elem_2MB;
        elem_2MB.valid = 1;
        elem_2MB.value = address >> 24; // We can assume 8 PTE entries/hash set
        std::vector<elem_t> accessedAddresses_2MB;
        if (page_size == 21)
            accessedAddresses_2MB = find_elastic_cuckoo_potm(&elem_2MB, &elasticCuckooHT_2MB, m_mem_info);

		bool found = false;
		bool found4KB = false;
		bool found2MB = false;
        if (page_size == 12) {
            for(elem_t elem: accessedAddresses_4KB) {
                if(elem.valid)
                {
                    found = true;
                    found4KB = true;
                    m_hits++;
                    break;
                }
            }
        }
        if (page_size == 21) {
            for(elem_t elem: accessedAddresses_2MB) {
                if(elem.valid)
                {
                    found = true;
                    found2MB = true;
                    m_hits++;
                    break;
                }
            }
        }

		if(!found) {
			if(page_size == 12) {
                insert_elastic(&elem_4KB, &elasticCuckooHT_4KB, 0, 0);
                // evaluate_elasticity(&elasticCuckooHT_4KB,0);
			}
            else {
                insert_elastic(&elem_2MB, &elasticCuckooHT_2MB, 0, 0);
                // evaluate_elasticity(&elasticCuckooHT_2MB, 0);
            }
            m_miss++;
        }
        SubsecondTime maxLat = SubsecondTime::Zero();
        SubsecondTime t_start = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);

        if(page_size == 12) {
            if(found4KB || !found) {
                for(elem_t addr: accessedAddresses_4KB) {  
                    IntPtr cache_address = addr.value & (~((64 - 1))); 		
                    CacheBlockInfo::block_type_t block_type = CacheBlockInfo::block_type_t::TLB_ENTRY;
                    l1dcache->processMemOpFromCore(
                        0,
                        lock_signal,
                        Core::mem_op_t::READ,
                        cache_address, 0,
                        NULL, 64,
                        true,
                        true, block_type, SubsecondTime::Zero());
                    SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
                    shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
                    if((t_end - t_start) > maxLat) 
                        maxLat = t_end - t_start;
                }
            }
        }
        if(page_size == 21) {
            if(found2MB || !found){
                for(elem_t addr: accessedAddresses_2MB) {
                    IntPtr cache_address = addr.value & (~((64 - 1))); 		
                    CacheBlockInfo::block_type_t block_type =  CacheBlockInfo::block_type_t::TLB_ENTRY;
                    l1dcache->processMemOpFromCore(
                        0,
                        lock_signal,
                        Core::mem_op_t::READ,
                        cache_address, 0,
                        NULL, 64,
                        true,
                        true, block_type, SubsecondTime::Zero());
                    SubsecondTime t_end = shmem_perf_model->getElapsedTime(ShmemPerfModel::_USER_THREAD);
                    shmem_perf_model->setElapsedTime(ShmemPerfModel::_USER_THREAD, t_start);
                    if((t_end - t_start) > maxLat) 
                        maxLat = t_end - t_start;
                }
            }
        }
		if(found) {
			cuckoo_latency += maxLat;
            return where_t::HIT;
		}
		else {
			cuckoo_latency += maxLat;
            return where_t::MISS;
		}               
    }
}