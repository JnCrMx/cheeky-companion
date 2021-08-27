#include "layer.hpp"
#include "logger.hpp"
#include "net/server.hpp"
#include "net/handler.hpp"

#include <memory>
#include <thread>
#include <iostream>

using namespace network;

std::unique_ptr<network_handler> handlerFactory(clientID client, std::string name, class server* server)
{
	return std::make_unique<network_handler>(client, name, server);
}

int main()
{
	std::ofstream out("/dev/stdout");
	::logger = new CheekyLayer::logger(out);
	server* server = new basic_server(9001, &handlerFactory);

	std::this_thread::sleep_for(std::chrono::seconds(10));

	delete server;
}