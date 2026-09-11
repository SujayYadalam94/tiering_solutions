// Diagnostic of one-address versus whole-region residency on the current kernel.
// Changes only this process's private 2 MiB test region; no global controls.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/mempolicy.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

constexpr size_t REGION = 2 * 1024 * 1024;
constexpr size_t BASE = 4096;

static void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "%s: errno=%d (%s)\n", what, errno, std::strerror(errno));
        std::exit(1);
    }
}

static void snapshot(const char* phase, std::vector<void*>& addresses,
                     size_t expected_near, int expected_first) {
    std::vector<int> status(addresses.size(), -100);
    long rc = syscall(SYS_move_pages, 0, addresses.size(), addresses.data(),
                      nullptr, status.data(), 0);
    check(rc == 0, "query all 512 base pages");
    size_t near=0, far=0, other=0;
    for (int s:status) {
        if (s==0) ++near;
        else if (s==1) ++far;
        else ++other;
    }
    std::printf("{\"phase\":\"%s\",\"query_return\":%ld,\"first_page_node\":%d,"
                "\"near_base_pages\":%zu,\"far_base_pages\":%zu,\"other_statuses\":%zu,"
                "\"actual_near_bytes\":%zu,\"near_bytes_if_first_page_represents_region\":%zu}\n",
                phase, rc, status[0], near, far, other, near*BASE,
                status[0]==0 ? REGION : size_t(0));
    std::fflush(stdout);
    check(near == expected_near && status[0] == expected_first && other == 0 && near+far == 512,
          "unexpected measured residency");
}

static void move(std::vector<void*>& addresses, size_t count, int node) {
    std::vector<int> target(count, node), status(count, -100);
    long rc = syscall(SYS_move_pages, 0, count, addresses.data(), target.data(),
                      status.data(), MPOL_MF_MOVE);
    std::printf("{\"operation\":\"move\",\"submitted_addresses\":%zu,\"target_node\":%d,\"return\":%ld}\n", count,node,rc);
    check(rc == 0, "move this process's test pages");
    for (int s:status) check(s == node, "migration status differs from target");
}

int main() {
    check(sysconf(_SC_PAGESIZE) == BASE, "requires 4 KiB base pages");
    void* raw = mmap(nullptr, REGION*2, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    check(raw != MAP_FAILED, "mmap private diagnostic region");
    auto aligned = (reinterpret_cast<uintptr_t>(raw)+REGION-1) & ~(REGION-1);
    auto* region = reinterpret_cast<unsigned char*>(aligned);
    check(madvise(region,REGION,MADV_NOHUGEPAGE)==0, "make diagnostic region base pages");
    unsigned long nodes = 1UL << 1;
    check(syscall(SYS_mbind,region,REGION,MPOL_BIND,&nodes,sizeof(nodes)*8,0)==0,
          "bind only diagnostic region to node 1");
    std::vector<void*> addresses;
    for (size_t i=0;i<REGION;i+=BASE) {
        region[i]=1;
        addresses.push_back(region+i);
    }
    snapshot("all_far",addresses,0,1);
    move(addresses,1,0);
    snapshot("first_page_promoted",addresses,1,0);
    move(addresses,addresses.size(),0);
    snapshot("all_near",addresses,512,0);
    move(addresses,1,1);
    snapshot("first_page_demoted",addresses,511,1);
    check(munmap(raw,REGION*2)==0,"release diagnostic region");
    return 0;
}
