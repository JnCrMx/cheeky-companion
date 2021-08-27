#pragma once

#include "packets.hpp"
#include "handler.hpp"

#include <cstddef>
#include <future>
#include <memory>
#include <thread>
#include <map>

namespace network
{
	class server
	{
		public:
			using handler_factory = std::unique_ptr<network_handler>(*)(clientID client, std::string name, server* server);

			server(handler_factory factory) : m_handlerFactory(factory) {}
			virtual ~server();
			virtual void send(clientID client, clientbound::PacketType type, void* data, size_t size) = 0;
			virtual void disconnect(clientID client) = 0;
		protected:
			std::thread m_thread;
			std::promise<void> m_exit;
			
			handler_factory m_handlerFactory;
			std::map<int, std::unique_ptr<network_handler>> m_clientHandlers;

			void handleData(clientID client, serverbound::PacketType type, void* data, size_t size);
			void handleDisconnect(clientID client);

			clientID nextClient(std::string name);
		private:
			clientID nextClientID = 1;
	};

	class basic_server : public server
	{
		public:
			basic_server(int port, handler_factory factory);
			~basic_server() override;
			void send(clientID client, clientbound::PacketType type, void* data, size_t size) override;
			virtual void disconnect(clientID client) override;
		private:
			int m_fd;

			std::map<clientID, int> m_clientSockets;
			std::map<clientID, std::thread> m_receivingThreads;

			virtual void acceptingThread(std::shared_future<void> exit);
			virtual void receivingThread(clientID client, std::shared_future<void> exit);
	};
}