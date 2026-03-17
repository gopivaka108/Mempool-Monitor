#include "AnvilSimulator.h"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <nlohmann/json.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

bool simulate_trade(std::string from, std::string to, std::string value, std::string data) {
    try {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        stream.connect(resolver.resolve("127.0.0.1", "8545"));

        json call_params = {
            {{"from", from}, {"to", to}, {"value", value}, {"data", data}},
            "latest"
        };

        json rpc_payload = {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "eth_call"}, {"params", call_params}
        };

        http::request<http::string_body> req{http::verb::post, "/", 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = rpc_payload.dump();
        req.prepare_payload();

        http::write(stream, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        stream.socket().shutdown(tcp::socket::shutdown_both);

        json response = json::parse(res.body());
        
        if (response.contains("error")) {
            std::cout << "    [X] SIMULATION: Transaction will REVERT (Failed/Scam)\n";
            return false;
        } else {
            std::cout << "    [+] SIMULATION: Transaction SUCCESS\n";
            return true;
        }
    } catch(...) {
        return false;
    }
}