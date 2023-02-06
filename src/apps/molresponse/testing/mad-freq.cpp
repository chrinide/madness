//
// Created by adrianhurtado on 1/1/22.
//
#include "ResponseExceptions.hpp"
#include "madness/tensor/tensor_json.hpp"
#include "madness/world/worldmem.h"
#include "response_functions.h"
#include "runners.hpp"

#if defined(HAVE_SYS_TYPES_H) && defined(HAVE_SYS_STAT_H) && defined(HAVE_UNISTD_H)

#include <sys/stat.h>
#include <unistd.h>

static inline int file_exists(const char *input_name) {
    struct stat buffer {};
    size_t rc = stat(input_name, &buffer);
    return (rc == 0);
}

#endif

using path = std::filesystem::path;

using namespace madness;


auto main(int argc, char *argv[]) -> int {

    madness::initialize(argc, argv);

    {
        World world(SafeMPI::COMM_WORLD);
        startup(world, argc, argv, true);

        try {
            sleep(5);
            std::cout.precision(6);
            if (argc != 5) {
                std::cout << "Wrong number of inputs" << std::endl;
                return 1;
            }
            const std::string molecule_name{argv[1]};
            const std::string xc{argv[2]};
            const std::string op{argv[3]};
            const std::string precision{argv[4]};
            if (precision != "high" && precision != "low" && precision != "super") {
                if (world.rank() == 0) {
                    std::cout << "Set precision to low high super" << std::endl;
                }
                return 1;
            }
            auto schema = runSchema(world, xc);
            auto m_schema = moldftSchema(world, molecule_name, xc, schema);
            auto f_schema = frequencySchema(world, schema, m_schema, op);
            if (std::filesystem::exists(m_schema.calc_info_json_path) &&
                std::filesystem::exists(m_schema.moldft_restart)) {
                runFrequencyTests(world, f_schema, precision);
            } else {
                moldft(world, m_schema, true, false, precision);
                runFrequencyTests(world, f_schema, precision);
                world.gop.fence();
                world.gop.fence();
            }
        } catch (const SafeMPI::Exception &e) {
            print(e.what());
            error("caught an MPI exception");
        } catch (const madness::MadnessException &e) {
            print(e);
            error("caught a MADNESS exception");
        } catch (const madness::TensorException &e) {
            print(e.what());
            error("caught a Tensor exception");
        } catch (const char *s) { print(s); } catch (const std::string &s) {
            print(s);
        } catch (const nlohmann::detail::exception &e) {
            std::cout<<e.what();
            error("caught an JSON exception");
        } catch (const std::exception &e) {
            print(e.what());
            error("caught an STL exception");
        } catch (...) { error("caught unhandled exception"); }
        // Nearly all memory will be freed at this point
        print_stats(world);
        if (world.rank() == 0) { print("Finished All Frequencies"); }
    }
    finalize();
    return 0;
}
