#ifndef TRAFFIC_GENERATOR_H
#define TRAFFIC_GENERATOR_H

#define TRAFFIC_GENERATOR_MODEL 1

#include <fstream>
#include <assert.h>
#include <traffic_models/traffic_model_base.h>
#include <utils/terminal_logging.h>

//--------------------------------------------------------------------------------------------------
// traffic_generator(): simple traffic generator model in which, in every time step, it reads how many
// bits need to be generated according to a traffic generation file. One file for each tx direction. 
// Input: 
//      id: unique id of the UE used to initialize the random generator's seed. 
//      _ul_traffic_file/_dl_traffic_file: file name and directory for the UL and DL traffic generation
//                                         data.
//      _pkt_size: virtual IP packet size in bits.
//--------------------------------------------------------------------------------------------------
class traffic_generator: public traffic_model_base
{
public:
    traffic_generator( int id,
                       std::string _ul_traffic_file,
                       std::string _dl_traffic_file,
                       int _pkt_size, float _delay=0)
    {
        ul_traffic_file = _ul_traffic_file; 
        dl_traffic_file = _dl_traffic_file;
        std::cout << "UL traffic : " << ul_traffic_file << "\n";
        std::cout << "DL traffic : " << dl_traffic_file << "\n";
        pkt_size = _pkt_size; 
        ul_file.open(ul_traffic_file);
        dl_file.open(dl_traffic_file);
        delay = _delay;
        if(delay>0) delay+= uniform_dist(uniform_gen)*0.0166;
        assert(ul_file.good());
        assert(dl_file.good());
        LOG_INFO_I("traffic_generator::traffic_generator") << " Could find files for UL [" << ul_traffic_file << "]and DL [" << dl_traffic_file << "] traffic generator models, loading . . ." << END(); 
    }
    traffic_generator(){}
    
public:

    float generate(int tx, double t)
    {
		assert(tx == T_DL || tx == T_UL); 
        if(tx == T_DL) return generate_dl(t);
        else return generate_ul(t); 
    }

    float generate_ul(double t)
    {
        t-=delay;
        float total_tp = 0.0;
        if(last_t_ul <= t) total_tp += last_tp_ul; 
        while(last_t_ul <= t)
        {   
            if(ul_file >> last_t_ul >> last_tp_ul)
            {
                if(last_t_ul <= t)
                {
                    total_tp += last_tp_ul;
                    last_t_ul += t_acc_ul;
                }
            }
            else
            {
                t_acc_ul = last_t_ul;
                reset_file(ul_file);
                //assert(ul_file >> last_t_ul >> last_tp_ul);
                if(ul_file >> last_t_ul >> last_tp_ul)
                {
                    if(last_t_ul <= t)
                    {
                        total_tp += last_tp_ul;
                        last_t_ul += t_acc_ul;
                    }
                }
            }
        }
        return total_tp*8;  
    }

    float generate_dl(double t)
    {
        t-=delay;
        float total_tp = 0.0;
        if(last_t_dl <= t) total_tp += last_tp_dl; 
        while(last_t_dl <= t)
        {
            if(dl_file >> last_t_dl >> last_tp_dl)
            {
                if(last_t_dl <= t)
                {
                    total_tp += last_tp_dl;
                    last_t_dl += t_acc_dl;
                }
            }
            else
            {
                t_acc_dl = last_t_dl;
                reset_file(dl_file);
                //assert(dl_file >> last_t_dl >> last_tp_dl);
                if(dl_file >> last_t_dl >> last_tp_dl)
                {
                    if(last_t_dl <= t)
                    {
                        total_tp += last_tp_dl;
                        last_t_dl += t_acc_dl;
                    }
                }
            }
        }
        if (t > dumb_t)
        {
            dumb_t += 1;
            dumb_tp = 0.0;
        }
        dumb_tp += total_tp*8; 
        return total_tp*8; 
    }
private: 
    void reset_file(std::fstream& file)
    {
        file.clear();
        file.seekg(0);
    }

private: 
    std::string ul_traffic_file;
    std::string dl_traffic_file; 
    std::fstream dl_file; 
    std::fstream ul_file; 
    float delay=0; 

private: 
    float dumb_tp = 0.0; 
    int dumb_t = 1; 
    float last_tp_ul = 0.0;
    double last_t_ul = 0.0;
    double t_acc_ul = 0.0;

private: 
    float last_tp_dl = 0.0;
    double last_t_dl = 0.0;
    double t_acc_dl = 0.0;
};
#endif