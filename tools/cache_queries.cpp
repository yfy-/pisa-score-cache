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
#include "type_alias.hpp"
#include "term_map.hpp"

using namespace pisa;
using ranges::views::enumerate;
using ScoreCache = std::unordered_map<Query, Score>;

struct QueryResult {
  const Query& query;
  Score init_threshold;
  Score k_threshold;
  Score final_threshold;
  std::vector<double> times;
  QueryResult(const Query& q) : query(q), init_threshold(0), k_threshold(0),
                                final_threshold(0) {}
};

void report_result(const std::vector<QueryResult>& results,
                   const std::string& file) {
  std::ofstream fs(file);
  if (!fs.is_open()) {
    spdlog::error("Cannot open {}", file);
    return;
  }

  for (auto& r: results) {
    if (r.query.terms.empty())
      continue;

    fs << *r.query.id << "\t";
    fs << r.query.terms[0];
    for (auto qb = r.query.terms.begin() + 1, qe = r.query.terms.end();
         qb != qe; ++qb)
      fs << "," << *qb;
    fs << "\t" << r.init_threshold << "\t" << r.k_threshold << "\t" <<
        r.final_threshold << "\t";
    const std::vector<double>& r_times = r.times;
    fs << r_times[0];
    for (auto tb = r_times.begin() + 1, te = r_times.end(); tb != te; ++tb)
      fs << "\t" << *tb;

    fs << "\n";
  }

  fs.close();
}

template <typename Functor>
std::vector<QueryResult> op_perftest(
    Functor query_func,
    std::vector<Query> const& queries,
    const ScoreCache& score_cache,
    std::string const& index_type,
    std::string const& query_type,
    std::uint64_t k,
    size_t runs)
{
  size_t num_query = queries.size();
  std::vector<std::vector<double>> query_times(num_query);
  std::vector<QueryResult> results;
  for (size_t run = 0; run <= runs; ++run) {
    size_t idx = 0;
    spdlog::info("Run id: {}", run);
    for (auto const& query: queries) {
      auto start_time = std::chrono::steady_clock::now();
      QueryResult result = query_func(query, score_cache);
      auto end_time = std::chrono::steady_clock::now();
      auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(
          end_time - start_time);

      if (run)
        query_times[idx].push_back(usecs.count());

      // Add result only once
      if (run == 1)
        results.push_back(std::move(result));

      idx += 1;
    }
  }

  for (int i = 0; i < num_query; ++i)
    results[i].times = std::move(query_times[i]);

  return results;
}

Score cache_search(const Query& query, const ScoreCache& cache) {
  if (cache.empty())
    return 0.0;

  auto exact_found = cache.find(query);
  if (exact_found != end(cache))
    return exact_found->second;

  std::vector<Query> subqueries;
  for (size_t i = 3; i > 0; --i) {
    std::vector<Query> sq = query.subsets(i);
    subqueries.insert(std::end(subqueries), std::begin(sq), std::end(sq));
  }

  Score cache_score = 0.0;
  for (auto q : subqueries) {
    auto found = cache.find(q);
    if (found == end(cache))
      continue;

    cache_score = std::max(cache_score, found->second);
  }

  return cache_score;
}

template <typename QueryType>
QueryResult query_execute(
    const Query& query, const ScoreCache& cache, uint64_t k,
    const std::function<void(const QueryType&)>& callback) {
    Score init_threshold = cache_search(query, cache);
    topk_queue topk(k, /* initial_threshold= */ init_threshold);
    QueryType qrun(topk);
    callback(qrun);
    topk.finalize();
    QueryResult res(query);
    res.init_threshold = init_threshold;
    res.k_threshold = topk.threshold_at_k();
    res.final_threshold = topk.true_threshold();
    return res;
}

using AlgoResult = std::pair<std::string, std::vector<QueryResult>>;

template <typename IndexType, typename WandType>
std::vector<AlgoResult> perftest(
    const std::string& index_filename,
    const std::optional<std::string>& wand_data_filename,
    const std::vector<Query>& queries,
    const std::optional<std::string>& thresholds_filename,
    const QueryParser& query_parser,
    const std::optional<std::string>& terms_filename,
    std::string const& type,
    std::string const& query_type,
    uint64_t k,
    uint32_t runs,
    const ScorerParams& scorer_params)
{
    spdlog::info("Loading index from {}", index_filename);
    IndexType index(MemorySource::mapped_file(index_filename));

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

    WandType const wdata = [&] {
        if (wand_data_filename) {
            return WandType(MemorySource::mapped_file(*wand_data_filename));
        }
        return WandType{};
    }();

    std::vector<std::string> query_lines;
    std::vector<Score> scores;
    if (thresholds_filename) {
      spdlog::info("Loading score cache");
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
    auto parse_query = [&cache_queries, &query_parser](auto&& line) {
        cache_queries.push_back(query_parser.parse(line));
    };
    std::for_each(std::begin(query_lines), std::end(query_lines), parse_query);
    ScoreCache score_cache;
    for (int i = 0; i < scores.size(); ++i) {
      if (!cache_queries[i].terms.size())
        continue;

      score_cache[cache_queries[i]] = scores[i];
    }

    auto scorer = scorer::from_params(scorer_params, wdata);
    spdlog::info("Performing {} queries", type);
    spdlog::info("K: {}", k);
    spdlog::info("Query log size: {}", queries.size());
    std::vector<std::string> query_types;
    boost::algorithm::split(query_types, query_type, boost::is_any_of(":"));
    std::vector<AlgoResult> algo_results;
    for (auto&& t: query_types) {
      std::function<QueryResult(const Query&, const ScoreCache&)> query_fun;
      spdlog::info("Query type: {}", t);
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
        continue;
      }

      algo_results.emplace_back(t, op_perftest(query_fun, queries, score_cache,
                                               type, query_type, k, runs));
    }

    return algo_results;
}

using wand_raw_index = wand_data<wand_data_raw>;
using wand_uniform_index = wand_data<wand_data_compressed<>>;
using wand_uniform_index_quantized = wand_data<wand_data_compressed<PayloadType::Quantized>>;

int main(int argc, const char** argv)
{
    bool silent = false;
    bool safe = false;
    bool quantized = false;
    bool result_cache_filter = false;
    uint32_t runs = 2;
    std::string out_basename = "";

    App<arg::Index,
        arg::WandData<arg::WandMode::Optional>,
        arg::Query<arg::QueryMode::Ranked>,
        arg::Algorithm,
        arg::Scorer,
        arg::Thresholds>
        app{"Benchmarks queries on a given index."};
    app.add_option("-o,--output", out_basename, "Output basename")->required();
    app.add_flag("--quantized", quantized, "Quantized scores");
    app.add_flag("--silent", silent, "Suppress logging");
    app.add_flag("--result-cache-filter", result_cache_filter,
                 "Do not run if query exactly hits score cache/result cache");
    app.add_option("--runs", runs,
                   "Number of runs (initial warm-up run will be already done)");
    CLI11_PARSE(app, argc, argv);

    if (!runs)
      runs = 1;

    if (silent) {
        spdlog::set_default_logger(spdlog::create<spdlog::sinks::null_sink_mt>("stderr"));
    } else {
        spdlog::set_default_logger(spdlog::stderr_color_mt("stderr"));
    }

    // TODO: use result_cache_filter
    auto params = std::make_tuple(
        app.index_filename(),
        app.wand_data_path(),
        app.queries(),
        app.thresholds_file(),
        app.query_parser(),
        app.term_lexicon(),
        app.index_encoding(),
        app.algorithm(),
        app.k(),
        runs,
        app.scorer_params());
    std::vector<AlgoResult> results;
    /**/
    if (false) {
#define LOOP_BODY(R, DATA, T)                                                                        \
    }                                                                                                \
    else if (app.index_encoding() == BOOST_PP_STRINGIZE(T))                                          \
    {                                                                                                \
        if (app.is_wand_compressed()) {                                                              \
            if (quantized) {                                                                         \
                results = std::apply(perftest<BOOST_PP_CAT(T, _index), wand_uniform_index_quantized>, params); \
            } else {                                                                                 \
                results = std::apply(perftest<BOOST_PP_CAT(T, _index), wand_uniform_index>, params);           \
            }                                                                                        \
        } else {                                                                                     \
            results = std::apply(perftest<BOOST_PP_CAT(T, _index), wand_raw_index>, params);                   \
        }
        /**/
        BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, PISA_INDEX_TYPES);
#undef LOOP_BODY

    } else {
        spdlog::error("Unknown type {}", app.index_encoding());
    }

    out_basename += "_" + app.index_encoding() + "_" + std::to_string(app.k()) +
                    "_" + app.scorer_params().name;
    for (const auto& ar: results)
      report_result(ar.second, out_basename + "." + ar.first);

    return 0;
}
