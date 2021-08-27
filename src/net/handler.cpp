#include "net/handler.hpp"
#include "companion.hpp"
#include "net/packets.hpp"
#include "net/server.hpp"

#include "logger.hpp"
#include "layer.hpp"

#include "shared.hpp"

#include <algorithm>

using CheekyLayer::logger;

namespace network
{
	void network_handler::send(clientbound::PacketType type, void* data, size_t size)
	{
		m_server->send(m_clientID, type, data, size);
	}

	void network_handler::disconnect(clientbound::DisconnectReason reason)
	{
		clientbound::DisconnectPacket packet{.reason = reason};
		m_server->send(m_clientID, clientbound::PacketType::Disconnect, &packet, sizeof(packet));
		m_server->disconnect(m_clientID);
	}

	void network_handler::handleDisconnect()
	{
		auto it = std::find(clients.begin(), clients.end(), m_renderClient);
		if(it != clients.end())
			clients.erase(it);

		if(m_renderClient)
			delete m_renderClient;
	}

	void network_handler::handlePacket(serverbound::PacketType type, void* data, size_t size)
	{
		*::logger << logger::begin << type << ": " << data << " of " << size << logger::end;
		if(type == serverbound::PacketType::Join)
		{
			serverbound::JoinPacket* join = (serverbound::JoinPacket*)data;
			std::string name = join->name;
			std::string companion = join->companion;

			*::logger << logger::begin << "Join: " << companion << " from " << name << logger::end;

			if(clients.size() >= mainConfig["maxClients"])
				disconnect(clientbound::DisconnectReason::TooManyClients);
			if(!companions.contains(companion))
				disconnect(clientbound::DisconnectReason::UnknownCompanion);

			m_renderClient = new render_client(companion);
			m_renderClient->init(globalDevice);

			clients.push_back(m_renderClient);
		}
	}
}
