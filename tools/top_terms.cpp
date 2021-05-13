#include <iostream>
#include <string>
#include <unordered_map>
#include <numeric>
#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "app.hpp"
#include "topk_queue.hpp"

using namespace pisa;

int main(int argc, const char** argv) {
  spdlog::set_default_logger(spdlog::stderr_color_mt("default"));
  App<arg::Query<arg::QueryMode::Ranked>> app(
      "Compute a query with top-k scoring terms");
  CLI11_PARSE(app, argc, argv);
  spdlog::info("Counting term frequencies from {}", app.query_file()->get());
  std::unordered_map<term_id_type, std::uint32_t> term_counts;
  for (Query& q: app.queries()) {
    for (term_id_type tid: q.terms)
      ++term_counts[tid];
  }

  spdlog::info("Ranking top-{} of {} unique terms", app.k(),
               term_counts.size());
  topk_queue queue(app.k());
  for (auto& [tid, freq]: term_counts)
    queue.insert(freq, tid);

  queue.finalize();
  const std::vector<topk_queue::entry_type>& topk = queue.topk();
  auto beg = std::begin(topk);
  std::cout << beg->second;
  for (auto it = std::next(beg), e = std::end(topk); it != e; ++it)
    std::cout << " " << it->second;

  std::cout << "\n";
  return 0;
}
