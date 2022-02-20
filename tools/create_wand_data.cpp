#include "CLI/CLI.hpp"
#include "app.hpp"
#include "wand_data.hpp"

int main(int argc, const char** argv)
{
    CLI::App app{"Creates additional data for query processing."};
    pisa::CreateWandDataArgs args(&app);
    std::optional<std::string> global_input_basename;
    app.add_option("-g,--global-collection", global_input_basename);
    CLI11_PARSE(app, argc, argv);
    pisa::create_wand_data(
        args.output(),
        args.input_basename(),
        global_input_basename,
        args.block_size(),
        args.scorer_params(),
        args.range(),
        args.compress(),
        args.quantize(),
        args.dropped_term_ids());
}
