/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <fstream>

#include <utils/control/ndjson.h>
#include <utils/control/transport_file.h>
#include <utils/terminal_logging.h>

transport_file::transport_file(const std::string &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        LOG_ERROR_I("transport_file") << " cannot open timeline file: " << path << END();
        return;
    }

    std::string text;
    std::uint64_t line_no = 0;
    while (std::getline(file, text))
    {
        line_no++;
        if (text.empty() || text[0] == '#') continue;

        std::string error;
        if (!ndjson::parse_line(text, line_no, pending_, error))
        {
            LOG_ERROR_I("transport_file") << " invalid line " << line_no
                                           << ": " << error << END();
            pending_.clear();
            return;
        }
    }
    if (file.bad() || !file.eof())
    {
        LOG_ERROR_I("transport_file") << " I/O error while reading timeline: "
                                      << path << END();
        pending_.clear();
        return;
    }

    ok_ = true;
    LOG_INFO_I("transport_file") << " loaded " << pending_.size() << " commands from " << path << END();
}

bool transport_file::poll(std::vector<command> &out)
{
    if (delivered_) return false;
    delivered_ = true;
    out.insert(out.end(), pending_.begin(), pending_.end());
    pending_.clear();
    return false;   // nothing else will ever arrive from a file
}

void transport_file::reply(const ack &a)
{
    if (a.ok) return;
    for (size_t i = 0; i < a.errors.size(); i++)
    {
        LOG_ERROR_I("transport_file") << " command " << a.id << " rejected: "
                                      << a.errors[i].key << ": " << a.errors[i].reason << END();
    }
}
