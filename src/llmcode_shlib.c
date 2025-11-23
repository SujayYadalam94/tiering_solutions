#include "hemem.h"
#include "pebs.h"

// Scoring function to be evolved using PolicySmith
// This function can be overridden via LD_PRELOAD
float scoring_function(const struct hemem_page *page, const struct global_stats *g_stats) {
  // Implement scoring logic here
  float score = 0.0;

  return score;
}
