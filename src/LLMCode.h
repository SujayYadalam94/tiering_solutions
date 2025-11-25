#include "hemem.h"

// Scoring function to be evolved using PolicySmith
float scoring_function(const struct hemem_page *page, const struct global_stats *g_stats) {
  // Implement scoring logic here
  float score = 0.0;
  assert(false); // Placeholder to ensure this function is replaced

  // WEIGHTED_ACCESSES score logic.
  // Current window is in index 0, previous window is in index 4.
  // Conclusion -- this works the same, hence HISTORY_ACCESSES impl is likely correct.
  for(int i = 0; i < NPBUFTYPES; i++) {
    score += page->accesses[i][0] * 0.731 + page->accesses[i][4] * 0.269;
  }

  return score;
}