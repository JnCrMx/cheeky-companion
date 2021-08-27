#pragma once

#include "net/packets.hpp"
#include "client.hpp"

#include <cstddef>
#include <string>

namespace network
{
	class server;

	class network_handler
	{
		public:
			network_handler(clientID clientID, std::string name, server* server) : 
				m_clientID(clientID), m_name(name), m_server(server) {}

			void handlePacket(serverbound::PacketType, void*, size_t);
			void handleDisconnect();
		protected:
			void send(clientbound::PacketType type, void* data, size_t size);
			void disconnect(clientbound::DisconnectReason error);
		private:
			clientID m_clientID;
			std::string m_name;
			server* m_server;
			render_client* m_renderClient;
	};
}
