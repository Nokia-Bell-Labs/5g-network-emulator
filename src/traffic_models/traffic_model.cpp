#include <traffic_models/traffic_model.h>

traffic_model::traffic_model(int id, traffic_config traffic_c)
{
    traffic_c.ul_target = traffic_c.ul_target * MBIT2BIT;
    traffic_c.dl_target = traffic_c.dl_target * MBIT2BIT;
    if(traffic_c.type == CONSTANT_TRAFFIC_MODEL)
    {
        traff_model = std::unique_ptr<traffic_model_base>(new traffic_model_base(id, traffic_c.ul_target, traffic_c.dl_target, traffic_c.var_perc, traffic_c.pkt_size, traffic_c.random_v));
    }
    else if(traffic_c.type == TRAFFIC_GENERATOR_MODEL)
    {
        traff_model = std::unique_ptr<traffic_model_base>(new traffic_generator(id, traffic_c.ul_traffic_file, traffic_c.dl_traffic_file, traffic_c.pkt_size, traffic_c.delay));
    }
    else
    {
        traff_model = std::unique_ptr<traffic_model_base>(new traffic_model_base(id, traffic_c.ul_target, traffic_c.dl_target, traffic_c.var_perc, traffic_c.pkt_size, traffic_c.random_v));
    }
}

float traffic_model::generate(int tx, double t)
{
    return traff_model->generate(tx, t);
}

int traffic_model::get_pkt_size(int tx)
{
    return traff_model->get_pkt_size(tx);
}

void traffic_model::set_target(int tx, float bps)
{
    traff_model->set_target(tx, bps);
}

float traffic_model::get_target(int tx) const
{
    return traff_model->get_target(tx);
}
