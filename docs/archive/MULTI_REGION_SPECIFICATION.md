We are going to rework how the runtime policy manages the assignment of pages to different policies.

I will break the behavior I want down into steps.

# Step 1, configuration

We specify what regions exist and what VA range they cover with the env variable HEMEM_REGIONS example:
`HEMEM_REGIONS="0x555555400000-0x7fff1f600000:lru,0x7fff1f800000-0x7fff97800000:lfu"`

So this gives us two regions, for now call them LRU, LFU. There is also an implicit third region called the "fallback" region. This fallback region covers the entire address space, and operates as lfu. The role is to catch any memory accesses that fall outside of the manually specificed ranges.

We elso have an env variable `HEMEM_REGION_PHYS` which specified what allocation size to give each region from our DAX devices that emulate DRAM and NVM. Example:
`HEMEM_REGION_PHYS="lru:4G:16G,lfu:8G:16G"`

This will give the LRU policy 4 GB of DRAM, 16 GB of NVM, and LFU gets 8 GB of DRAM, 16 GB of NVM. This allocation is done during setup by creating lists of free huge pages (2 MB) to give to each policy so they can manage it.

**IMPORTANT** We manage pages at the policy granularity, not region granularity. So if we have multiple LFU or LRU regions, they will be aggregated and managed by a singular LRU or LFU policy thread. We still need the region bounds though so we can lookup whether a VA belongs to an LRU of LFU region.

**IMPORTANT** Following the above, the catchall region is an LFU region, so it should be merged with any other LFU regions.

Reprsenting the VA ranges with set notation:

@LFU = Union of all LFU VA ranges
@LRU = Union of all LRU VA ranges

#LFU VA Range = (@LFU | Catchall) \ @LRU
#LRU VA Range =  @LRU \ (@LFU | Catchall)

LRU and LFU ranges are mutually exclusive.

## Physical Memory Assignment Example


Say we have a DAX config of DRAMSIZE 20G, NVMSIZE 32 G and we have
`HEMEM_REGION_PHYS="lru:4G:16G,lfu:8G:16G"`

Then LRU get's 4 G and 16 G of DRAM and NVM

The LFU gets 8 G and 16 G of DRAM and NVM

We have 20 - 4 - 8 = 8 G of DRAM leftover and 32 - 16 - 16 = 0 of NVM leftover
So the default region gets 8 G DRAM, 0 G NVM.

But the default region is LFU so its assignment is merged with the current LFU.

So LFU has 8 + 8 = 16 G DRAM, and 16 + 0 G = 16 NVM

# Step 2, Resource Allocation

To reiterate, once we have calculated the total number of 2 MB pages to give
each policy, we split up the DAX devices into page lists for DRAM/NVM for each
policy. We should keep a handle to these page lists so we know what pages each
policy is managing.

# Step 3, Policy Initialization

Now our bootstrap function should call the respective init functions for the
different policies, passing them the page lists for DRAM/NVM. Since the
policy is just starting up, it will take those pages and place them on its
free list. Each policy may have its own data structures to track the pages,
but those initial pages are given to it by the bootstrapper.

Let's start with this for now.

