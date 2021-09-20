#include <fstream>
#include <unordered_map>

#include <CLI/CLI.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "app.hpp"

using namespace pisa;

int main(int argc, const char** argv) {
  spdlog::set_default_logger(spdlog::stderr_color_mt("default"));
  App<arg::Query<arg::QueryMode::Ranked>> app(
      "Creates a score cache from TREC results and the query log that produced"
      " those results with `evaluate_queries`");
  std::string trec_file;
  app.add_option("-t,--trec", trec_file, "TREC file")->required();
  CLI11_PARSE(app, argc, argv);
  std::uint32_t k = app.k();
  std::ifstream trec_stream(trec_file);
  std::string trec_line;
  std::unordered_map<std::string, float> kth_scores;
  while (std::getline(trec_stream, trec_line)) {
    std::stringstream tok_stream(trec_line);
    int tok_id = 0;
    std::string tok;
    std::string qid;
    std::uint32_t qrank = 0;
    while (std::getline(tok_stream, tok, '\t')) {
      if (tok_id == 0) {
          qid = tok;
      } else if (tok_id == 3) {
        qrank = std::stoull(tok);
        if (qrank + 1 != k)
          break;
      } else if (tok_id == 4) {
        kth_scores[qid] = std::stof(tok);
        break;
      }

      tok_id++;
    }
  }

  std::optional<std::string> lex_file = app.term_lexicon();
  if (lex_file) {
    mio::mmap_source m(lex_file->c_str());
    auto payload = Payload_Vector<>::from(m);
    for (const Query& q: app.queries()) {
      std::cout << payload[q.terms[0]];

        for (std::vector<term_id_type>::const_iterator tid =
                 std::next(std::begin(q.terms));
             tid != std::end(q.terms);
             ++tid)
          std::cout << " " << payload[*tid];

        std::cout << ";" << kth_scores[*q.id] << "\n";
    }
  } else {
      for (const Query& q: app.queries()) {
          std::cout << q.terms[0];

          for (std::vector<term_id_type>::const_iterator tid =
                   std::next(std::begin(q.terms));
               tid != std::end(q.terms);
               ++tid)
              std::cout << " " << *tid;

          std::cout << ";" << kth_scores[*q.id] << "\n";
      }
  }

  return 0;
}
