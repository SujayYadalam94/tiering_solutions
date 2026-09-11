
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <memory>
#include <shared_mutex>
#include <vector>
constexpr int FAST_TIER = 0, SLOW_TIER = 1;
constexpr bool VIRTUAL_FEATURES_ENABLED = false;
struct page_info {
  uint64_t va=0, last_seen_scan=0, last_access_generation=0;
  bool found_in_pebs=false, in_dram=false, fragmented=false;
};
using page_ptr = std::shared_ptr<page_info>;
std::shared_mutex virtual_features_lock;
page_ptr get_tracked_page(uint64_t) { return nullptr; }
bool has_pending_virtual_accesses(const page_ptr&) { return false; }
long numa_move_pages(int, unsigned long count, void**, const int*, int* status, int) {
  // No kernel call: return one synthetic node-0 status per submitted address.
  for (unsigned long i=0; i<count; ++i) status[i]=FAST_TIER;
  return 0;
}

static void process_batch(std::vector<void *> &addr_batch, std::vector<int> &status_batch,
                          std::vector<page_ptr> &to_add_near, std::vector<page_ptr> &to_add_far,
                          std::vector<uint64_t> &to_remove, int &total_dram, int &total_cxl, uint64_t cur_scan)
{
    long ret = numa_move_pages(0, static_cast<unsigned long>(addr_batch.size()), addr_batch.data(), nullptr,
                               status_batch.data(), 0);
    if (ret != 0)
    {
        perror("[ARMS] Warning: numa_move_pages batch failed");
        return;
    }

    int fast = 0;
    int slow = 0;
    int error = 0;

    for (int i = 0; i < status_batch.size(); i++)
    {
        int status = status_batch[i];
        uint64_t va = reinterpret_cast<uint64_t>(addr_batch[i]);

        if (status == FAST_TIER)
        {
            fast++;
            total_dram++;
            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;
                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_near.push_back(page);
            }
            page->in_dram = true;
            page->fragmented = false;
        }
        else if (status == SLOW_TIER)
        {
            slow++;
            total_cxl++;

            auto page = get_tracked_page(va);
            if (page == nullptr)
            {
                page = std::make_shared<page_info>();
                page->va = va;

                page->found_in_pebs = false;
                page->last_seen_scan = cur_scan;
                page->last_access_generation = cur_scan;
                to_add_far.push_back(page);
            }
            page->in_dram = false;
            page->fragmented = false;
        }
        else if (status == -EFAULT || status == -ENOENT)
        {
            // Page not present or inaccessible, treat as not in DRAM
            auto page = get_tracked_page(va);
            if (page != nullptr)
            {
                if (page->found_in_pebs)
                {
                    page->found_in_pebs = false;
                    page->fragmented = true;
                }
                else if (!page->fragmented)
                {
                    bool keep_for_virtual_flush = false;
                    if (VIRTUAL_FEATURES_ENABLED)
                    {
                        std::shared_lock<std::shared_mutex> virtual_lock(virtual_features_lock);
                        keep_for_virtual_flush = has_pending_virtual_accesses(page);
                    }

                    if (!keep_for_virtual_flush)
                    {
                        to_remove.push_back(va);
                    }
                }
            }
        }
        else if (status < 0)
        {
            error++;
        }
    }
    addr_batch.clear();
}


int main(int argc, char** argv) {
  if (argc!=2) return 2;
  const size_t count=std::strtoul(argv[1],nullptr,10);
  if (count>128) return 2;
  std::vector<void*> addresses;
  addresses.reserve(128);
  for (size_t i=0; i<count; ++i)
    addresses.push_back(reinterpret_cast<void*>((i+1)*0x200000ULL));
  std::vector<int> statuses(128);
  std::vector<page_ptr> near, far;
  std::vector<uint64_t> remove;
  int dram=0,cxl=0;
  process_batch(addresses,statuses,near,far,remove,dram,cxl,1);
  std::cout << "submitted=" << count << " processed=" << dram+cxl << '\n';
  return (near.size()==count && far.empty() && dram==static_cast<int>(count)) ? 0 : 3;
}
