#include "query_pool.h"


std::unique_ptr<TermPoolArray> CreateTermPoolArray(const TermList &query,
    const int n_pools) {
  std::unique_ptr<TermPoolArray> array(new TermPoolArray(n_pools));
  array->LoadTerms(query);
  return array;
}

std::unique_ptr<TermPoolArray> CreateTermPoolArray(
    const std::string &query_log_path, const int n_pools, const int n_queries) 
{
  std::unique_ptr<TermPoolArray> array(new TermPoolArray(n_pools));
  array->LoadFromFile(query_log_path, n_queries);
  return array;
}


