#include "page.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
static float dram_boundary_score_moving_average=0;
static uint32_t dram_boundary_score_moving_average_count=0;
float adjusted_ewma(const float yp, const float x, const float denom)
{
    return yp * (1 - (1 / denom)) + (x / denom);
}
static void update_dram_boundary_score_moving_average(const std::vector<score_entry> &scores, int64_t max_hugepages)
{
    if (max_hugepages < 0 || static_cast<size_t>(max_hugepages) >= scores.size())
    {
        return;
    }

    const float boundary_score = scores[static_cast<size_t>(max_hugepages) - 250].score;
    const float denom = get_adjusted_ewma_denom(3, dram_boundary_score_moving_average_count);
    dram_boundary_score_moving_average = adjusted_ewma(dram_boundary_score_moving_average, boundary_score, denom);
    dram_boundary_score_moving_average_count++;
}
int main(int argc,char**argv){std::vector<score_entry> scores(1000); long cap=argc>1?std::strtol(argv[1],nullptr,10):249; update_dram_boundary_score_moving_average(scores,cap); std::printf("updates=%u value=%f\n",dram_boundary_score_moving_average_count,dram_boundary_score_moving_average);}
