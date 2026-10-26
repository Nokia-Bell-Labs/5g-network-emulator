/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <simulator/simulator.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utils/run_status.h>

int main(int argc, char** argv)
{
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [config.ini]\n";
        return exit_status(run_exit_code::usage);
    }

    std::string config_file;
    if (argc > 1) {
        config_file = argv[1];
        std::cout << "Config file: " << config_file << "\n";
    } else {
        config_file = "./config.ini";
        std::cout << "Config file by default: " << config_file << "\n";
    }

    std::ifstream config(config_file);
    if (!config.is_open())
    {
        std::cerr << "Cannot open configuration file: " << config_file << "\n";
        return exit_status(run_exit_code::no_input);
    }
    config.close();

    try
    {
        simulator sim(config_file);
        sim.start();
        sim.join();
        sim.print_traffic();
        return sim.exit_code();
    }
    catch (const run_failure &e)
    {
        std::cerr << "FikoRE startup failed: " << e.what() << "\n";
        return exit_status(e.code());
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << "Invalid configuration: " << e.what() << "\n";
        return exit_status(run_exit_code::config);
    }
    catch (const std::out_of_range &e)
    {
        std::cerr << "Configuration value out of range: " << e.what() << "\n";
        return exit_status(run_exit_code::config);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal simulator error: " << e.what() << "\n";
        return exit_status(run_exit_code::software);
    }
}

