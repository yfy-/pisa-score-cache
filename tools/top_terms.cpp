#include <cmath>

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

using namespace pisa;

template <typename WandType>
int run(std::vector<::pisa::Query>&& queries, int k, WandType* wand_data)
{
    std::unordered_map<term_id_type, std::uint32_t> term_counts;
    for (Query& q: queries) {
      for (term_id_type tid: q.terms)
        ++term_counts[tid];
    }

    spdlog::info("Ranking top-{} of {} unique terms", k, term_counts.size());
    topk_queue queue(k);
    std::function<float(term_id_type)> idf_calc = [](term_id_type tid) {
      return 1.0;
    };

    // If wand-data is given scorer will be tf-idf
    if (wand_data) {
      // IDF smooth
      idf_calc = [&wand_data](term_id_type tid) -> float {
        return log2(static_cast<float>(wand_data->num_docs()) /
                    (wand_data->term_posting_count(tid) + 1)) + 1;
      };
    }

    for (auto& [tid, freq]: term_counts) {
      // Log normalized tf
      queue.insert(log2(freq + 1) * idf_calc(tid), tid);
    }

    queue.finalize();
    const std::vector<topk_queue::entry_type>& topk = queue.topk();
    auto beg = std::begin(topk);
    std::cout << beg->second;
    for (auto it = std::next(beg), e = std::end(topk); it != e; ++it)
        std::cout << " " << it->second;

    return 0;
}

int main(int argc, const char** argv) {
  spdlog::set_default_logger(spdlog::stderr_color_mt("default"));
  spdlog::set_level(spdlog::level::level_enum::debug);
  App<arg::Query<arg::QueryMode::Ranked>,
      arg::WandData<arg::WandMode::Optional>> app(
      "Compute a query with top-k scoring terms");
  bool quantized = false;
  app.add_flag("--quantized", quantized, "Quantized wand-data scores.");
  CLI11_PARSE(app, argc, argv);
  spdlog::info("Counting term frequencies from {}", app.query_file()->get());
  spdlog::info("Number of queries (empty ones removed) {}",
               app.queries().size());
  int err = 0;
  if (app.wand_data_path()) {
    MemorySource wand_mapped = MemorySource::mapped_file(*app.wand_data_path());
    if (app.is_wand_compressed()) {
      if (quantized) {
        wand_data<wand_data_compressed<PayloadType::Quantized>> wdata(
            std::move(wand_mapped));
        return run(app.queries(), app.k(), &wdata);
      } else {
        wand_data<wand_data_compressed<>> wdata(std::move(wand_mapped));
        return run(app.queries(), app.k(), &wdata);
      }
    } else {
      wand_data<wand_data_raw> wdata(std::move(wand_mapped));
      return run(app.queries(), app.k(), &wdata);
    }
  }

  return run<wand_data<wand_data_raw>>(app.queries(), app.k(), nullptr);
}
