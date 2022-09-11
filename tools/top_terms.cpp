#include <iostream>
#include <string>
#include <unordered_map>
#include <numeric>
#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <vector>

#include "app.hpp"
#include "topk_queue.hpp"
#include "wand_data.hpp"
#include "wand_data_raw.hpp"
#include "wand_data_compressed.hpp"
#

using namespace pisa;

double uniform_idf(term_id_type tid) { return 1; }

int main(int argc, const char** argv) {
  spdlog::set_default_logger(spdlog::stderr_color_mt("default"));
  App<arg::Query<arg::QueryMode::Ranked>,
      arg::WandData<arg::WandMode::Optional>> app(
      "Compute a query with top-k scoring terms");
  bool quantized = false;
  app.add_flag("--quantized", quantized, "Quantized wand-data scores.");
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


  double (*idf_calc)(term_id_type) = &uniform_idf;
  auto scorer = [](std::uint32_t tf, std::uint32_t idf) -> double {
    return tf * idf;
  };

  // If wand-data is given scorer will be tf-idf
  if (app.wand_data_path()) {
    MemorySource wand_mapped = MemorySource::mapped_file(*app.wand_data_path());
    if (app.is_wand_compressed()) {
      if (quantized) {
        wand_data<wand_data_compressed<PayloadType::Quantized>> wdata(
            std::move(wand_mapped));

        // FIXME: We need to figure this out
        idf_calc = &wdata.term_posting_count;
      } else {
        wand_data<wand_data_compressed<>> wdata(std::move(wand_mapped));
      }
    } else {
      wand_data<wand_data_raw> wdata(std::move(wand_mapped));
    }
  }

  for (auto& [tid, freq]: term_counts)
    queue.insert(scorer(freq, idf_calc(tid)));

  queue.finalize();
  const std::vector<topk_queue::entry_type>& topk = queue.topk();
  auto beg = std::begin(topk);
  std::cout << beg->second;
  for (auto it = std::next(beg), e = std::end(topk); it != e; ++it)
    std::cout << " " << it->second;

  std::cout << "\n";
  return 0;
}
