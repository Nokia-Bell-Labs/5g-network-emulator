/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <utils/control/ndjson.h>
#include <utils/control/proto_version.h>
#include <utils/control/transport_socket.h>
#include <utils/terminal_logging.h>

using json = nlohmann::json;

namespace
{
// Blocks are short and the peer is local, so a modest poll timeout is enough to notice
// that the emulator is shutting down.
const int POLL_TIMEOUT_MS = 200;
const int SEND_TIMEOUT_MS = 1000;
}

transport_socket::transport_socket(const control_config &cfg)
    : cfg_(cfg)
{
    if (cfg_.transport == "unix")
    {
        unix_path_ = cfg_.address;
        ::unlink(unix_path_.c_str());

        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
        {
            LOG_ERROR_I("transport_socket") << " cannot create unix socket: " << std::strerror(errno) << END();
            return;
        }

        struct sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, unix_path_.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(listen_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            LOG_ERROR_I("transport_socket") << " cannot bind " << unix_path_ << ": " << std::strerror(errno) << END();
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }

        // Owner only: the control channel can detach UEs and rewrite their parameters.
        ::chmod(unix_path_.c_str(), S_IRUSR | S_IWUSR);
    }
    else
    {
        const bool loopback = (cfg_.address == "127.0.0.1" || cfg_.address == "localhost");
        if (!loopback)
        {
            LOG_WARNING_I("transport_socket")
                << " control channel bound to " << cfg_.address << ", outside loopback:"
                << " there is no authentication on this channel" << END();
        }

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0)
        {
            LOG_ERROR_I("transport_socket") << " cannot create tcp socket: " << std::strerror(errno) << END();
            return;
        }

        int reuse = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((uint16_t)cfg_.port);
        if (::inet_pton(AF_INET, loopback && cfg_.address == "localhost" ? "127.0.0.1" : cfg_.address.c_str(),
                        &addr.sin_addr) != 1)
        {
            LOG_ERROR_I("transport_socket") << " bad address: " << cfg_.address << END();
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }

        if (::bind(listen_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            LOG_ERROR_I("transport_socket") << " cannot bind " << cfg_.address << ":" << cfg_.port
                                            << ": " << std::strerror(errno) << END();
            ::close(listen_fd_);
            listen_fd_ = -1;
            return;
        }
    }

    if (::listen(listen_fd_, 4) < 0)
    {
        LOG_ERROR_I("transport_socket") << " listen failed: " << std::strerror(errno) << END();
        ::close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    thread_ = std::thread(&transport_socket::serve, this);

    if (cfg_.transport == "unix")
        LOG_INFO_I("transport_socket") << " listening on unix:" << unix_path_ << " (mode 0600)" << END();
    else
        LOG_INFO_I("transport_socket") << " listening on tcp:" << cfg_.address << ":" << cfg_.port << END();
}

transport_socket::~transport_socket()
{
    stop();
}

void transport_socket::stop()
{
    if (stopping_.exchange(true)) return;

    int client = -1;
    {
        std::lock_guard<std::mutex> session_lk(session_mtx_);
        client = client_fd_.exchange(-1);
        active_generation_.store(0);
        if (client >= 0) ::shutdown(client, SHUT_RDWR);
    }
    if (listen_fd_ >= 0) ::shutdown(listen_fd_, SHUT_RDWR);

    if (thread_.joinable()) thread_.join();

    if (client >= 0) ::close(client);
    if (listen_fd_ >= 0)
    {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!unix_path_.empty()) ::unlink(unix_path_.c_str());
}

bool transport_socket::send_line(int fd, const std::string &line)
{
    if (fd < 0) return false;
    std::lock_guard<std::mutex> lk(write_mtx_);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now()
        + std::chrono::milliseconds(SEND_TIMEOUT_MS);
    ssize_t written = 0;
    while (written < (ssize_t)line.size())
    {
        if (std::chrono::steady_clock::now() >= deadline)
        {
            ::shutdown(fd, SHUT_RDWR);
            return false;
        }
        const ssize_t n = ::send(fd, line.data() + written, line.size() - written,
                                 MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0)
        {
            written += n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            const int remaining_ms = (int)std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline - now).count();
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLOUT;
            pfd.revents = 0;
            if (::poll(&pfd, 1, std::max(remaining_ms, 1)) > 0) continue;
        }
        ::shutdown(fd, SHUT_RDWR);
        return false;
    }
    if (written == (ssize_t)line.size()) return true;
    ::shutdown(fd, SHUT_RDWR);
    return false;
}

// No negotiation: both sides state the same constant or the connection is dropped. The
// emulator and the API are built together, so a mismatch is a deployment error, not a
// case to be compatible with.
bool transport_socket::handshake(int fd)
{
    json hello;
    hello["op"] = "hello";
    hello["proto"] = FIKORE_CONTROL_PROTO;
    if (!send_line(fd, hello.dump() + "\n")) return false;

    std::string line;
    char ch = 0;
    while (!stopping_.load())
    {
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (ready < 0) return false;
        if (ready == 0) continue;

        const ssize_t n = ::recv(fd, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') break;
        line.push_back(ch);
        if (line.size() > 4096) return false;
    }

    try
    {
        json reply = json::parse(line);
        if (reply.contains("proto") && reply["proto"].get<std::string>() == FIKORE_CONTROL_PROTO)
            return true;
    }
    catch (const std::exception &)
    {
    }

    json err;
    err["op"] = "error";
    err["reason"] = "protocol mismatch, emulator speaks " FIKORE_CONTROL_PROTO;
    send_line(fd, err.dump() + "\n");
    LOG_WARNING_I("transport_socket") << " client rejected: protocol mismatch" << END();
    return false;
}

// Single event loop over the listening socket and the current client. Serving the client
// from a nested loop would leave a second connection sitting in the backlog instead of
// being told that the channel is taken.
void transport_socket::serve()
{
    std::string buffer;
    char chunk[4096];

    while (!stopping_.load())
    {
        struct pollfd pfds[2];
        pfds[0].fd = listen_fd_;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;

        const int client = client_fd_.load();
        int nfds = 1;
        if (client >= 0)
        {
            pfds[1].fd = client;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;
            nfds = 2;
        }

        const int ready = ::poll(pfds, nfds, POLL_TIMEOUT_MS);
        if (ready <= 0) continue;

        if (pfds[0].revents & POLLIN)
        {
            const int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0)
            {
                if (client_fd_.load() >= 0)
                {
                    json err;
                    err["op"] = "error";
                    err["reason"] = "control channel already in use";
                    send_line(fd, err.dump() + "\n");
                    ::close(fd);
                    LOG_WARNING_I("transport_socket") << " second connection refused: channel in use" << END();
                }
                else if (!handshake(fd))
                {
                    ::close(fd);
                }
                else
                {
                    buffer.clear();
                    peer_ever_connected_.store(true);
                    {
                        std::lock_guard<std::mutex> session_lk(session_mtx_);
                        active_generation_.store(next_generation_++);
                        client_fd_.store(fd);
                    }
                    LOG_INFO_I("transport_socket") << " client connected, proto " << FIKORE_CONTROL_PROTO << END();
                }
            }
        }

        if (nfds == 2 && (pfds[1].revents & (POLLIN | POLLHUP | POLLERR)))
        {
            const ssize_t n = ::recv(client, chunk, sizeof(chunk), 0);
            if (n <= 0)
            {
                {
                    std::lock_guard<std::mutex> session_lk(session_mtx_);
                    if (client_fd_.load() == client)
                    {
                        active_generation_.store(0);
                        client_fd_.store(-1);
                        ::close(client);
                    }
                }
                LOG_INFO_I("transport_socket") << " client disconnected" << END();
                continue;
            }

            buffer.append(chunk, (size_t)n);
            handle_lines(client, active_generation_.load(), buffer);
        }
    }
}

void transport_socket::handle_lines(int fd, std::uint64_t generation, std::string &buffer)
{
    size_t nl;
    while ((nl = buffer.find('\n')) != std::string::npos)
    {
        const std::string line = buffer.substr(0, nl);
        buffer.erase(0, nl + 1);
        if (line.empty()) continue;

        std::vector<command> parsed;
        std::string error;
        line_no_++;
        std::uint64_t message_id = line_no_;
        if (!ndjson::parse_line(line, line_no_, parsed, error, &message_id))
        {
            ack a;
            a.id = message_id;
            a.ok = false;
            ack_error e;
            e.key = "message";
            e.reason = error;
            a.errors.push_back(e);
            send_line(fd, ndjson::serialize_ack(a));
            continue;
        }

        std::vector<command> queued;
        for (size_t i = 0; i < parsed.size(); i++)
        {
            parsed[i].connection_generation = generation;
            // Grants are the one exception to "apply only from the simulation thread":
            // in barrier mode that thread is blocked waiting for exactly this, so it
            // cannot drain the inbox. A grant touches only the manager's credit counter.
            if (parsed[i].op == command_op::grant && grant_sink_)
            {
                ack a;
                a.id = parsed[i].id;
                a.connection_generation = generation;
                grant_sink_(parsed[i], a);
                send_line(fd, ndjson::serialize_ack(a));
                continue;
            }
            queued.push_back(parsed[i]);
        }

        if (queued.empty()) continue;

        // The rest is picked up by the simulation thread at the next quiescent point.
        std::lock_guard<std::mutex> lk(inbox_mtx_);
        inbox_.insert(inbox_.end(), queued.begin(), queued.end());
    }
}

bool transport_socket::peer_alive() const
{
    return client_fd_.load() >= 0;
}

bool transport_socket::peer_ever_connected() const
{
    return peer_ever_connected_.load();
}

bool transport_socket::command_is_current(const command &c) const
{
    return c.connection_generation != 0
        && c.connection_generation == active_generation_.load();
}

std::uint64_t transport_socket::current_generation() const
{
    return active_generation_.load();
}

bool transport_socket::poll(std::vector<command> &out)
{
    std::lock_guard<std::mutex> lk(inbox_mtx_);
    if (!inbox_.empty())
    {
        out.insert(out.end(), inbox_.begin(), inbox_.end());
        inbox_.clear();
    }
    return !stopping_.load();   // a socket keeps producing until the emulator stops
}

void transport_socket::reply(const ack &a)
{
    std::lock_guard<std::mutex> session_lk(session_mtx_);
    if (a.connection_generation != active_generation_.load()) return;
    send_line(client_fd_.load(), ndjson::serialize_ack(a));
}
