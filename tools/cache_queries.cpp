#include <algorithm>
#include <iostream>
#include <numeric>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <range/v3/view/enumerate.hpp>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "accumulator/lazy_accumulator.hpp"
#include "app.hpp"
#include "cursor/block_max_scored_cursor.hpp"
#include "cursor/cursor.hpp"
#include "cursor/max_scored_cursor.hpp"
#include "cursor/scored_cursor.hpp"
#include "index_types.hpp"
#include "mappable/mapper.hpp"
#include "memory_source.hpp"
#include "query/algorithm.hpp"
#include "query/queries.hpp"
#include "scorer/scorer.hpp"
#include "timer.hpp"
#include "topk_queue.hpp"
#include "util/util.hpp"
#include "wand_data_compressed.hpp"
#include "wand_data_raw.hpp"

using namespace pisa;
using ranges::views::enumerate;
using ScoreCache = std::unordered_map<Query, Threshold>;

struct QueryResult {
  const Query& query;
  Threshold init_threshold;
  Threshold final_threshold;

  QueryResult(const Query& q) : query(q), init_threshold(0),
                                final_threshold(0) {}
};

template <typename Functor>
void op_perftest(
    Functor query_func,
    std::vector<Query> const& queries,
    const ScoreCache& score_cache,
    std::string const& index_type,
    std::string const& query_type,
    std::uint64_t k,
    size_t runs,
    bool no_warm_up)
{
  size_t num_query = queries.size();
  std::vector<std::vector<double>> query_times(num_query);
  std::vector<QueryResult> results;
  for (size_t run = no_warm_up ? 1 : 0; run <= runs; ++run) {
    size_t idx = 0;
    spdlog::info("Run id: {}", run);
    for (auto const& query: queries) {
      spdlog::info("Executing query with id {}", idx);
      auto start_time = std::chrono::steady_clock::now();
      QueryResult result = query_func(query, score_cache);
      auto end_time = std::chrono::steady_clock::now();
      auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(
          end_time - start_time);
      // Add time except warm-up
      if (run)
        query_times[idx].push_back(usecs.count());

      // Add result only once
      if (run == 1)
        results.push_back(std::move(result));

      idx += 1;
    }
  }

  std::cout << "id;query_terms;init_threshold;final_threshold";
  for (size_t i = 0; i < runs; ++i)
    std::cout << ";time_ms_" << i;

  std::cout << "\n";
  for (size_t i = 0; i < num_query; ++i) {
    const QueryResult& res = results[i];
    if (res.query.terms.size()) {
      std::cout << i << ";";
      std::cout << res.query.terms[0];
      for (auto qb = res.query.terms.begin() + 1, qe = res.query.terms.end();
           qb != qe; ++qb)
        std::cout << " " << *qb;

      std::cout << ";" << res.init_threshold << ";" << res.final_threshold << ";";
      const std::vector<double>& res_times = query_times[i];
      std::cout << res_times[0];
      for (auto tb = res_times.begin() + 1, te = res_times.end(); tb != te;
           ++tb)
        std::cout << ";" << *tb;

      std::cout << "\n";
    }
  }
}

Threshold cache_search(const Query& query, const ScoreCache& cache) {
  std::vector<Query> subqueries;
  for (size_t i = 3; i > 0; --i) {
    std::vector<Query> sq = query.subsets(i);
    subqueries.insert(std::end(subqueries), std::begin(sq), std::end(sq));
  }

  Threshold cache_score = 0.0;
  for (auto q : subqueries) {
    auto found = cache.find(q);
    if (found == cache.end())
      continue;

    cache_score = max(cache_score, found->second);
  }

  return cache_score;
}

template <typename QueryType>
QueryResult query_execute(
    const Query& query, const ScoreCache& cache, uint64_t k,
    const std::function<void(const QueryType&)>& callback) {
  topk_queue topk(k);
  QueryResult res(query);
  res.init_threshold = cache_search(query, cache);
  topk.set_threshold(res.init_threshold);
  QueryType qrun(topk);
  callback(qrun);
  topk.finalize();
  res.final_threshold = topk.threshold();
  return res;
}

template <typename IndexType, typename WandType>
void perftest(
    const std::string& index_filename,
    const std::optional<std::string>& wand_data_filename,
    const std::vector<Query>& queries,
    const std::optional<std::string>& thresholds_filename,
    const std::optional<std::string>& stopwords_filename,
    const std::optional<std::string>& stemmer_type,
    std::string const& type,
    std::string const& query_type,
    uint64_t k,
    uint32_t runs,
    bool no_warm_up,
    const ScorerParams& scorer_params)
{
    spdlog::info("Loading index from {}", index_filename);
    IndexType index(MemorySource::mapped_file(index_filename));

    if (!no_warm_up) {
        spdlog::info("Warming up posting lists");
        std::unordered_set<term_id_type> warmed_up;
        for (auto const& q: queries) {
            for (auto t: q.terms) {
                if (!warmed_up.count(t)) {
                    index.warmup(t);
                    warmed_up.insert(t);
                }
            }
        }
    }

    WandType const wdata = [&] {
        if (wand_data_filename) {
            return WandType(MemorySource::mapped_file(*wand_data_filename));
        }
        return WandType{};
    }();

    std::vector<std::string> query_lines;
    std::vector<Threshold> scores;
    if (thresholds_filename) {
        std::string t;
        std::ifstream tin(*thresholds_filename);
        while (std::getline(tin, t)) {
          size_t i = t.find(";");
          if (i == std::string::npos) {
            spdlog::error("Invalid cache entry {}, "
                          "unable to load cache properly", t);
            break;
          }

          scores.push_back(std::stof(t.substr(i + 1)));
          query_lines.push_back(t.substr(0, i));
        }
    }

    std::vector<Query> cache_queries;
    std::transform(std::begin(query_lines), std::end(query_lines),
                   std::back_inserter(cache_queries), parse_query_ids);
    ScoreCache score_cache;
    for (int i = 0; i < scores.size(); ++i) {
      if (!cache_queries[i].terms.size())
        continue;

      score_cache[cache_queries[i]] = scores[i];
    }

    auto scorer = scorer::from_params(scorer_params, wdata);

    spdlog::info("Performing {} queries", type);
    spdlog::info("K: {}", k);

    std::vector<std::string> query_types;
    boost::algorithm::split(query_types, query_type, boost::is_any_of(":"));

    for (auto&& t: query_types) {
        spdlog::info("Query type: {}", t);
        std::function<QueryResult(const Query&, const ScoreCache&)> query_fun;
        if (t == "wand" && wand_data_filename) {
            query_fun = [&](const Query& query, const ScoreCache& cache) {
              return query_execute<wand_query>(
                  query, cache, k, [&](wand_query wand_q) {
                    wand_q(make_max_scored_cursors(index, wdata, *scorer,
                                                   query), index.num_docs());
                        });
            };
        } else if (t == "block_max_wand" && wand_data_filename) {
            query_fun = [&](const Query& query, const ScoreCache& cache) {
              return query_execute<block_max_wand_query>(
                  query, cache, k, [&](block_max_wand_query bmw_q) {
                    bmw_q(make_block_max_scored_cursors(index, wdata, *scorer,
                                                        query),
                          index.num_docs());
                  });
            };
        } else if (t == "block_max_maxscore" && wand_data_filename) {
            query_fun = [&](const Query& query, const ScoreCache& cache) {
              return query_execute<block_max_maxscore_query>(
                  query, cache, k, [&](block_max_maxscore_query bmm_q) {
                    bmm_q(make_block_max_scored_cursors(index, wdata, *scorer,
                                                        query),
                          index.num_docs());
                  });
            };
        } else if (t == "maxscore" && wand_data_filename) {
            query_fun = [&](const Query& query, const ScoreCache& cache) {
              return query_execute<maxscore_query>(
                  query, cache, k, [&](maxscore_query max_q) {
                    max_q(make_max_scored_cursors(index, wdata, *scorer,
                                                  query), index.num_docs());
                  });
            };
        } else {
            spdlog::error("Unsupported query type: {}", t);
            break;
        }

        op_perftest(query_fun, queries, score_cache, type, t, k, runs,
                    no_warm_up);
    }
}

using wand_raw_index = wand_data<wand_data_raw>;
using wand_uniform_index = wand_data<wand_data_compressed<>>;
using wand_uniform_index_quantized = wand_data<wand_data_compressed<PayloadType::Quantized>>;

int main(int argc, const char** argv)
{
    bool silent = false;
    bool safe = false;
    bool quantized = false;
    bool no_warm_up = false;
    uint32_t runs = 2;

    App<arg::Index,
        arg::WandData<arg::WandMode::Optional>,
        arg::Query<arg::QueryMode::Ranked>,
        arg::Algorithm,
        arg::Scorer,
        arg::Thresholds>
        app{"Benchmarks queries on a given index."};
    app.add_flag("--quantized", quantized, "Quantized scores");
    app.add_flag("--silent", silent, "Suppress logging");
    app.add_option("--runs", runs,
                   "Number of runs (initial warm-up run will be already done)");
    app.add_flag("--no-warm-up", no_warm_up, "Skip the warm-up run");
    CLI11_PARSE(app, argc, argv);

    if (!runs)
      runs = 1;

    if (silent) {
        spdlog::set_default_logger(spdlog::create<spdlog::sinks::null_sink_mt>("stderr"));
    } else {
        spdlog::set_default_logger(spdlog::stderr_color_mt("stderr"));
    }

    auto params = std::make_tuple(
        app.index_filename(),
        app.wand_data_path(),
        app.queries(),
        app.thresholds_file(),
        app.stop_words(),
        app.stemmer(),
        app.index_encoding(),
        app.algorithm(),
        app.k(),
        runs,
        no_warm_up,
        app.scorer_params());
    /**/
    if (false) {
#define LOOP_BODY(R, DATA, T)                                                                        \
    }                                                                                                \
    else if (app.index_encoding() == BOOST_PP_STRINGIZE(T))                                          \
    {                                                                                                \
        if (app.is_wand_compressed()) {                                                              \
            if (quantized) {                                                                         \
                std::apply(perftest<BOOST_PP_CAT(T, _index), wand_uniform_index_quantized>, params); \
            } else {                                                                                 \
                std::apply(perftest<BOOST_PP_CAT(T, _index), wand_uniform_index>, params);           \
            }                                                                                        \
        } else {                                                                                     \
            std::apply(perftest<BOOST_PP_CAT(T, _index), wand_raw_index>, params);                   \
        }
        /**/
        BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, PISA_INDEX_TYPES);
#undef LOOP_BODY

    } else {
        spdlog::error("Unknown type {}", app.index_encoding());
    }
}
